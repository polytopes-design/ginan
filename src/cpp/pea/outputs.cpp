
// #pragma GCC optimize ("O0")

#include "architectureDocs.hpp"

Output Outputs__()
{
	DOCS_REFERENCE(Trace_Files__);
	DOCS_REFERENCE(IGS_Files__);
	DOCS_REFERENCE(GPX__);
	DOCS_REFERENCE(JSON__);
	DOCS_REFERENCE(COST__);
	DOCS_REFERENCE(RTCM__);
	DOCS_REFERENCE(POS__);
	DOCS_REFERENCE(Mongo_Database__);
}


#include <chrono>
#include <thread>

#include "interactiveTerminal.hpp"
#include "minimumConstraints.hpp"
#include "rinexObsWrite.hpp"
#include "ntripBroadcast.hpp"
#include "inputsOutputs.hpp"
#include "rinexNavWrite.hpp"
#include "rinexObsWrite.hpp"
#include "rinexClkWrite.hpp"
#include "algebraTrace.hpp"
#include "rtsSmoothing.hpp"
#include "streamCustom.hpp"
#include "streamParser.hpp"
#include "navigation.hpp"
#include "orbexWrite.hpp"
#include "mongoWrite.hpp"
#include "streamRtcm.hpp"
#include "acsConfig.hpp"
#include "streamUbx.hpp"
#include "orbitProp.hpp"
#include "ionoModel.hpp"
#include "constants.hpp"
#include "sp3Write.hpp"
#include "metaData.hpp"
#include "receiver.hpp"
#include "summary.hpp"
#include "fileLog.hpp"
#include "biases.hpp"
#include "sinex.hpp"
#include "cost.hpp"
#include "sbas.hpp"
#include "enums.h"
#include "gpx.hpp"
#include "pos.hpp"

using boost::date_time::not_a_date_time;
using std::this_thread::sleep_for;
using std::max;

/** Replace macros for times with numeric values.
* Available replacements are "<DDD> <D> <WWWW> <YYYY> <YY> <MM> <DD> <HH> <hh> <mm> <LOGTIME>"
*/
void replaceTimes(
	string&						str,	///< String to replace macros within
	boost::posix_time::ptime	time)	///< Time to use for replacements
{
	string DDD;
	string D;
	string WWWW;
	string YYYY;
	string YY;
	string MM;
	string DD;
	string HH;
	string mm;

	if (!time.is_not_a_date_time())
	{
		string gpsWeek0 = "1980-01-06 00:00:00.000";
		auto gpsZero = boost::posix_time::time_from_string(gpsWeek0);
		string time_string = boost::posix_time::to_iso_string(time);

		auto tm = to_tm(time);
		std::ostringstream ss;
		ss << std::setw(3) << std::setfill('0') << tm.tm_yday+1;
		string ddd = ss.str();

		auto gpsWeek = (time - gpsZero);
		int weeks = gpsWeek.hours() / 24 / 7;
		ss.str("");
		ss << std::setw(4) << std::setfill('0') << weeks;
		string wwww = ss.str();

		DDD	= ddd;
		D	= std::to_string(tm.tm_wday);
		WWWW	= wwww;
		YYYY	= time_string.substr(0,		4);
		YY		= time_string.substr(2,		2);
		MM		= time_string.substr(4,		2);
		DD		= time_string.substr(6,		2);
		HH		= time_string.substr(9,		2);
		mm		= time_string.substr(11,	2);
	}

	bool replaced = false;

	replaced |= replaceString(str, "<LOGTIME>",	"<YYYY>-<MM>-<DD>_<HH>:<mm>",	false);
	replaced |= replaceString(str, "<DDD>",		DDD,							false);
	replaced |= replaceString(str, "<D>",		D,								false);
	replaced |= replaceString(str, "<WWWW>",	WWWW,							false);
	replaced |= replaceString(str, "<YYYY>",	YYYY,							false);
	replaced |= replaceString(str, "<YY>",		YY,								false);
	replaced |= replaceString(str, "<MM>",		MM,								false);
	replaced |= replaceString(str, "<DD>",		DD,								false);
	replaced |= replaceString(str, "<HH>",		HH,								false);
	replaced |= replaceString(str, "<hh>",		HH,								false);
	replaced |= replaceString(str, "<mm>",		mm,								false);

	if	(  YY.empty()
		&& replaced)
	{
		//replacing with nothing here may cause issues - kill the entire string to prevent damage
		str = "";
	}
}

void replaceTimes(
	vector<string>&				strs,
	boost::posix_time::ptime	time)
{
	for (auto& str : strs)
	{
		replaceTimes(str, time);
	}
}

/** Create directories if required
*/
void createDirectories(
	boost::posix_time::ptime	logptime)
{
	// Ensure the output directories exist
	for (auto directory : {
								acsConfig.sp3_directory,
								acsConfig.erp_directory,
								acsConfig.gpx_directory,
								acsConfig.pos_directory,
								acsConfig.ems_directory,
								acsConfig.log_directory,
								acsConfig.cost_directory,
								acsConfig.ionex_directory,
								acsConfig.orbex_directory,
								acsConfig.sinex_directory,
								acsConfig.trace_directory,
								acsConfig.clocks_directory,
								acsConfig.slr_obs_directory,
								acsConfig.ionstec_directory,
								acsConfig.rtcm_nav_directory,
								acsConfig.rtcm_obs_directory,
								acsConfig.orbit_ics_directory,
								acsConfig.rinex_obs_directory,
								acsConfig.rinex_nav_directory,
								acsConfig.raw_custom_directory,
								acsConfig.trop_sinex_directory,
								acsConfig.bias_sinex_directory,
								acsConfig.pppOpts.rts_directory,
								acsConfig.decoded_rtcm_json_directory,
								acsConfig.encoded_rtcm_json_directory,
								acsConfig.network_statistics_json_directory
							})
	{
		replaceTimes(directory, logptime);

		if (directory == ".")	continue;
		if (directory == "./")	continue;
		if (directory.empty())	continue;

		try
		{
			std::filesystem::create_directories(directory);
		}
		catch (...)
		{
			BOOST_LOG_TRIVIAL(error) << "Error: Could not create directory: \"" << directory << "\"";
		}
	}
}

map<string, string> fileNames;

/** Create new empty trace files only when required when the filename is changed
*/
void createTracefiles(
	ReceiverMap&	receiverMap,
	Network&		pppNet,
	Network&		ionNet)
{
	boost::posix_time::ptime logptime = currentLogptime();
	createDirectories(logptime);

	startNewMongoDb("PRIMARY",		logptime,	acsConfig.mongoOpts[E_Mongo::PRIMARY]	.database,	E_Mongo::PRIMARY);
	startNewMongoDb("SECONDARY",	logptime,	acsConfig.mongoOpts[E_Mongo::SECONDARY]	.database,	E_Mongo::SECONDARY);

	auto insertSuffix = [](string str, string suffix)
	{
		auto pos = str.find_last_of('.');
		if (pos == string::npos)
		{
			return str + suffix;
		}
		return str.substr(0, pos) + suffix + str.substr(pos);
	};

	for (auto rts : {false, true})
	{
		if	(	rts
			&&(	acsConfig.process_rts	== false
			||acsConfig.pppOpts.rts_lag	== 0))
		{
			continue;
		}


		string suff;
		string metaSuff;

		if (rts)
		{
			suff		= acsConfig.pppOpts.rts_smoothed_suffix;
			metaSuff	= SMOOTHED_SUFFIX;

			if (acsConfig.process_ppp)
			{
				bool newTraceFile = createNewTraceFile(pppNet.id, "Network",	not_a_date_time,	acsConfig.pppOpts.rts_filename,						pppNet.kfState.rts_basename);

				if (newTraceFile)
				{
	// 				std::cout << "\n" << "new trace file";
					std::remove((pppNet.kfState.rts_basename					).c_str());
					std::remove((pppNet.kfState.rts_basename + FORWARD_SUFFIX	).c_str());
					std::remove((pppNet.kfState.rts_basename + BACKWARD_SUFFIX	).c_str());
				}
			}

			if (acsConfig.process_ionosphere)
			{
				bool newTraceFile = createNewTraceFile(ionNet.id, "Network",	not_a_date_time,	acsConfig.pppOpts.rts_filename,						ionNet.kfState.rts_basename);

				if (newTraceFile)
				{
	// 				std::cout << "\n" << "new trace file";
					std::remove((ionNet.kfState.rts_basename					).c_str());
					std::remove((ionNet.kfState.rts_basename + FORWARD_SUFFIX	).c_str());
					std::remove((ionNet.kfState.rts_basename + BACKWARD_SUFFIX	).c_str());
				}
			}
		}

		bool newTraceFile = false;

		for (auto& [Sat, satNav] : nav.satNavMap)
		{
			if (acsConfig.output_satellite_trace)
			if (rts == false)
			{
				newTraceFile |= createNewTraceFile(Sat,			"Sats",			logptime,	acsConfig.satellite_trace_filename,							satNav.traceFilename,													true,	acsConfig.output_config);
			}
		}

		for (auto& [id, rec] : receiverMap)
		{
			if (acsConfig.output_receiver_trace)
			if (rts == false)
			{
				newTraceFile |= createNewTraceFile(id,			rec.source,		logptime,	acsConfig.receiver_trace_filename,							rec.traceFilename,														true,	acsConfig.output_config);
			}

			if (acsConfig.output_json_trace)
			if (rts == false)
			{
				newTraceFile |= createNewTraceFile(id,			rec.source,		logptime,	acsConfig.receiver_json_filename,							rec.jsonTraceFilename);
			}

			if (acsConfig.output_cost)
			{
				newTraceFile |= createNewTraceFile(id,			rec.source,		logptime,	insertSuffix(acsConfig.cost_filename,				suff),	pppNet.kfState	.metaDataMap[COST_FILENAME_STR	+ id	+ metaSuff]);
			}

			if (acsConfig.output_gpx)
			{
				newTraceFile |= createNewTraceFile(id,			rec.source,		logptime,	insertSuffix(acsConfig.gpx_filename,				suff),	pppNet.kfState	.metaDataMap[GPX_FILENAME_STR	+ id	+ metaSuff]);
			}

			if (acsConfig.output_pos)
			{
				newTraceFile |= createNewTraceFile(id,			rec.source,		logptime,	insertSuffix(acsConfig.pos_filename,				suff),	pppNet.kfState	.metaDataMap[POS_FILENAME_STR	+ id	+ metaSuff]);
			}
		}

		if (acsConfig.output_network_trace)
		{
			newTraceFile |= createNewTraceFile(pppNet.id,		"Network",		logptime,	insertSuffix(acsConfig.network_trace_filename,		suff),	pppNet.kfState	.metaDataMap[TRACE_FILENAME_STR			+ metaSuff],	true,	acsConfig.output_config);

			if (suff.empty())
			{
				pppNet.traceFilename = pppNet.kfState.metaDataMap[TRACE_FILENAME_STR];
			}
		}

		if (acsConfig.output_ionosphere_trace)
		{
			newTraceFile |= createNewTraceFile("IONO",			"Network",		logptime,	insertSuffix(acsConfig.ionosphere_trace_filename,	suff),	ionNet.kfState	.metaDataMap[TRACE_FILENAME_STR			+ metaSuff],	true,	acsConfig.output_config);

			if (suff.empty())
			{
				ionNet.traceFilename = ionNet.kfState.metaDataMap[TRACE_FILENAME_STR];
			}
		}

		if (acsConfig.output_ionex)
		{
			newTraceFile |= createNewTraceFile("",				"Network",		logptime,	insertSuffix(acsConfig.ionex_filename,				suff),	pppNet.kfState	.metaDataMap[IONEX_FILENAME_STR			+ metaSuff]);
		}

		if (acsConfig.output_ionstec)
		{
			newTraceFile |= createNewTraceFile("",				"Network",		logptime,	insertSuffix(acsConfig.ionstec_filename,			suff),	pppNet.kfState	.metaDataMap[IONSTEC_FILENAME_STR		+ metaSuff]);
		}

		if (acsConfig.output_trop_sinex)
		{
			newTraceFile |= createNewTraceFile(pppNet.id,		"Network",		logptime,	insertSuffix(acsConfig.trop_sinex_filename,			suff),	pppNet.kfState	.metaDataMap[TROP_FILENAME_STR			+ metaSuff]);
		}

		if (acsConfig.output_bias_sinex)
		{
			newTraceFile |= createNewTraceFile(pppNet.id, 		"Network",		logptime,	insertSuffix(acsConfig.bias_sinex_filename,			suff),	pppNet.kfState	.metaDataMap[BSX_FILENAME_STR			+ metaSuff]);
			newTraceFile |= createNewTraceFile(pppNet.id, 		"Network",		logptime,	insertSuffix(acsConfig.bias_sinex_filename,			suff),	ionNet.kfState	.metaDataMap[BSX_FILENAME_STR			+ metaSuff]);
		}

		if (acsConfig.output_erp)
		{
			newTraceFile |= createNewTraceFile(pppNet.id,		"Network",		logptime,	insertSuffix(acsConfig.erp_filename,				suff),	pppNet.kfState	.metaDataMap[ERP_FILENAME_STR			+ metaSuff]);
		}

		if (acsConfig.output_clocks)
		{
			auto singleFilenameMap	= getSysOutputFilenames(acsConfig.clocks_filename,	tsync, false);
			auto filenameMap		= getSysOutputFilenames(acsConfig.clocks_filename,	tsync);
			for (auto& [filename, dummy] : filenameMap)
			{
				newTraceFile |= createNewTraceFile(pppNet.id,	"Network",		logptime,	insertSuffix(filename,								suff),	fileNames[filename + metaSuff]);
			}

			pppNet.kfState.metaDataMap[CLK_FILENAME_STR	+ metaSuff] = insertSuffix(singleFilenameMap.begin()->first, suff);
		}

		if (acsConfig.output_sp3)
		{
			auto singleFilenameMap	= getSysOutputFilenames(acsConfig.sp3_filename,	tsync, false);
			auto filenameMap		= getSysOutputFilenames(acsConfig.sp3_filename,	tsync);
			for (auto& [filename, dummy] : filenameMap)
			{
				newTraceFile |= createNewTraceFile(pppNet.id,	"Network",		logptime,	insertSuffix(filename,								suff),	fileNames[filename + metaSuff]);
			}

			pppNet.kfState.metaDataMap[SP3_FILENAME_STR	+ metaSuff] = insertSuffix(singleFilenameMap.begin()->first, suff);
		}

		if (acsConfig.output_orbex)
		{
			auto singleFilenameMap	= getSysOutputFilenames(acsConfig.orbex_filename,	tsync, false);
			auto filenameMap		= getSysOutputFilenames(acsConfig.orbex_filename,	tsync);
			for (auto& [filename, dummy] : filenameMap)
			{
				newTraceFile |= createNewTraceFile(pppNet.id,	"Network",		logptime,	insertSuffix(filename,								suff),	fileNames[filename + metaSuff]);
			}

			pppNet.kfState.metaDataMap[ORBEX_FILENAME_STR	+ metaSuff] = insertSuffix(singleFilenameMap.begin()->first, suff);
		}

		if (acsConfig.output_sbas_ems)
		{
			newTraceFile |= createNewTraceFile("",				"Network",		logptime,	acsConfig.ems_filename,										pppNet.kfState	.metaDataMap[EMS_FILENAME_STR]);
		}

		if	(  rts
			&& newTraceFile)
		{
			spitFilterToFile(pppNet.kfState.metaDataMap, E_SerialObject::METADATA, pppNet.kfState.rts_basename + FORWARD_SUFFIX, acsConfig.pppOpts.queue_rts_outputs);
		}
	}

	if (acsConfig.output_log)
	{
		createNewTraceFile("",									"Network",		logptime,	acsConfig.log_filename,										FileLog::path_log);
	}

	if (acsConfig.output_ntrip_log)
	{
		for (auto& [id, stream_ptr] : ntripBroadcaster.ntripUploadStreams)
		{
			auto& stream = *stream_ptr;

			createNewTraceFile(id,								"NTRIP",		logptime,	acsConfig.ntrip_log_filename,								stream.networkTraceFilename);
		}

		for (auto& [id, streamParser_ptr] : streamParserMultimap)
		try
		{
			auto& ntripStream = dynamic_cast<NtripStream&>(streamParser_ptr->stream);

			createNewTraceFile(id,								"NTRIP",		logptime,	acsConfig.ntrip_log_filename,								ntripStream.networkTraceFilename);
		}
		catch(std::bad_cast& e){/* Ignore expected bad casts for different types */}
	}

	if (acsConfig.output_rinex_obs)
	for (auto& [id, rec] : receiverMap)
	{
		auto filenameMap = getSysOutputFilenames(acsConfig.rinex_obs_filename,	tsync, true, id);
		for (auto& [filename, dummy] : filenameMap)
		{
			createNewTraceFile(id,								rec.source,		logptime,	filename,													fileNames[filename]);
		}
	}

	if (acsConfig.output_rinex_nav)
	{
		auto filenameMap = getSysOutputFilenames(acsConfig.rinex_nav_filename,	tsync);
		for (auto& [filename, dummy] : filenameMap)
		{
			createNewTraceFile("Navs",							"Network",		logptime,	filename,													fileNames[filename]);
		}
	}

	for (auto& [id, streamParser_ptr] : streamParserMultimap)
	try
	{
		auto& rtcmParser = dynamic_cast<RtcmParser&>(streamParser_ptr->parser);

		if (acsConfig.output_decoded_rtcm_json)
		{
			createNewTraceFile(id, 				rtcmParser.rtcmMountpoint,		logptime,	acsConfig.decoded_rtcm_json_filename,						rtcmParser.rtcmTraceFilename);
		}

		for (auto nav : {false, true})
		{
			bool isNav = true;
			try
			{
				auto& obsStream = dynamic_cast<ObsStream&>(*streamParser_ptr);

				isNav = false;
			}
			catch(std::bad_cast& e){/* Ignore expected bad casts for different types */}

			if	( (acsConfig.record_rtcm_nav && isNav == true	&& nav == true)
				||(acsConfig.record_rtcm_obs && isNav == false	&& nav == false))
			{
				string filename;

				if (nav)	filename = acsConfig.rtcm_nav_filename;
				else		filename = acsConfig.rtcm_obs_filename;

				createNewTraceFile(id, 			rtcmParser.rtcmMountpoint,		logptime,	filename,													rtcmParser.recordFilename);
			}
		}
	}
	catch(std::bad_cast& e){/* Ignore expected bad casts for different types */}

	for (auto& [id, streamParser_ptr] : streamParserMultimap)
	try
	{
		auto& ubxParser = dynamic_cast<UbxParser&>(streamParser_ptr->parser);

		if (acsConfig.record_raw_ubx)
		{
			createNewTraceFile(id, 	streamParser_ptr->stream.sourceString,		logptime,	acsConfig.raw_ubx_filename,									ubxParser.raw_ubx_filename);
		}
	}
	catch(std::bad_cast& e){/* Ignore expected bad casts for different types */}

	for (auto& [id, streamParser_ptr] : streamParserMultimap)
	try
	{
		auto& customParser = dynamic_cast<CustomParser&>(streamParser_ptr->parser);

		if (acsConfig.record_raw_custom)
		{
			createNewTraceFile(id,	streamParser_ptr->stream.sourceString,		logptime,	acsConfig.raw_custom_filename,								customParser.raw_custom_filename);
		}
	}
	catch(std::bad_cast& e){/* Ignore expected bad casts for different types */}
}


void outputPredictedStates(
	Trace&			trace,
	KFState&		kfState)
{
	if (acsConfig.mongoOpts.output_predictions == +E_Mongo::NONE)
	{
		return;
	}

	InteractiveTerminal::setMode(E_InteractiveMode::PredictingStates);
	BOOST_LOG_TRIVIAL(info) << " ------- PREDICTING STATES            --------" << "\n";

	tuple<double, double>	forward = {+1, acsConfig.mongoOpts.forward_prediction_duration};
	tuple<double, double>	reverse = {-1, acsConfig.mongoOpts.reverse_prediction_duration};

	MongoStatesOptions mongoStatesOpts;
	mongoStatesOpts.suffix		= "/PREDICTED";
	mongoStatesOpts.force		= true;
	mongoStatesOpts.queue		= acsConfig.mongoOpts.queue_outputs;
	mongoStatesOpts.instances	= acsConfig.mongoOpts.output_predictions;
	mongoStatesOpts.updated		= tsync;

	for (auto& duo : {forward, reverse})
	{
		auto& [sign, duration] = duo;

		if (duration < 0)
		{
			continue;
		}

		GTime	startTime	= tsync + acsConfig.mongoOpts.prediction_offset;
		GTime	stopTime	= tsync + acsConfig.mongoOpts.prediction_offset + sign * duration;
		double	timeDelta	= sign * acsConfig.mongoOpts.prediction_interval;

		Orbits orbits = prepareOrbits(trace, kfState);

		GTime orbitsTime = tsync;

		KFState copyState = kfState;

		for (GTime time = startTime; sign * (time - stopTime).to_double() <= 0; time += timeDelta)
		{
			//remove orbits because they're done separately
			for (auto& [kfKey, index] : copyState.kfIndexMap)
			{
				if (kfKey.type == +KF::ORBIT)
				{
					copyState.removeState(kfKey);
				}
			}

			copyState.stateTransition(nullStream, time);

			auto sent_predictions = acsConfig.mongoOpts.sent_predictions;

			auto orbitIt	= std::find(sent_predictions.begin(), sent_predictions.end(), +KF::ORBIT);
			auto allIt		= std::find(sent_predictions.begin(), sent_predictions.end(), +KF::ALL);

			bool doOrbits	= orbitIt	!= sent_predictions.end();
			bool doAll		= allIt		!= sent_predictions.end();

			if (doOrbits)
				sent_predictions.erase(orbitIt);

			{
				KFState subState = copyState.getSubState(sent_predictions);

				mongoStates(subState, mongoStatesOpts);
			}

			if	( orbits.empty() == false
				&&( doOrbits
				  ||doAll))
			{
				OrbitIntegrator integrator;
				integrator.timeInit				= orbitsTime;

				double tgap = (time - orbitsTime).to_double();

				integrateOrbits(integrator, orbits, tgap, acsConfig.propagationOptions.integrator_time_step);

				BOOST_LOG_TRIVIAL(info) << "Propagated " << tgap << "s to " << time.to_string();

				orbitsTime = time;

				KFState propState;
				propState.time = time;

				int s = 6 * orbits.size();

				propState.x	.resize(s);
				propState.dx.resize(s);
				propState.P	.resize(s, s);

				int index = 0;
				for (int o = 0; o < orbits.size(); o++)
				{
					auto& orbit = orbits[o];

					for (auto& [key, i] : orbit.subState_ptr->kfIndexMap)
					{
						if (key.type != KF::ORBIT)
						{
							continue;
						}

						if (key.num < 3)	propState.x(index)			= orbit.pos(i);
						else				propState.x(index)			= orbit.vel(i-3);

											propState.P(index,	index)	= orbit.posVelSTM(i, i);

						propState.kfIndexMap[key] = index;
						index++;
					}
				}

				mongoStates(propState, mongoStatesOpts);
			}

			//update to allow use of just-written values
			mongoStatesAvailable(time, mongoStatesOpts);
		}
	}
}

void configureUploadingStreams()
{
	for (auto& [outLabel, outStreamData] : acsConfig.netOpts.uploadingStreamData)
	{
		auto it = ntripBroadcaster.ntripUploadStreams.find(outLabel);

		// Create stream if it does not already exist.
		if (it == ntripBroadcaster.ntripUploadStreams.end())
		{
			auto outStream_ptr = std::make_shared<NtripUploader>(outStreamData.url);
			auto& outStream = *outStream_ptr.get();
			ntripBroadcaster.ntripUploadStreams[outLabel] = std::move(outStream_ptr);

			it = ntripBroadcaster.ntripUploadStreams.find(outLabel);
		}

		auto& [label, outStream_ptr]	= *it;
		auto& outStream					= *outStream_ptr;

		outStream.streamConfig.rtcmMsgOptsMap		= outStreamData.rtcmMsgOptsMap;
		outStream.streamConfig.itrf_datum 			= outStreamData.itrf_datum;
		outStream.streamConfig.provider_id 			= outStreamData.provider_id;
		outStream.streamConfig.solution_id 			= outStreamData.solution_id;
	}

	for (auto it = ntripBroadcaster.ntripUploadStreams.begin(); it != ntripBroadcaster.ntripUploadStreams.end();)
	{
		if (acsConfig.netOpts.uploadingStreamData.find(it->first) == acsConfig.netOpts.uploadingStreamData.end())
		{
			auto& [label, outStream_ptr]	= *it;
			auto& outStream					= *outStream_ptr;
			outStream.disconnect();
			it = ntripBroadcaster.ntripUploadStreams.erase(it);
		}
		else
		{
			it++;
		}
	}

	if	( acsConfig.process_ppp				== false
		&&acsConfig.process_spp				== false
		&&acsConfig.slrOpts.process_slr		== false
		&&acsConfig.process_preprocessor	== false
		&&acsConfig.process_ionosphere		== false)
	while (1)
	{
		BOOST_LOG_TRIVIAL(info) << "Running with no processing modes enabled";

		sleep_for(std::chrono::seconds(10));
	}
}

void perEpochPostProcessingAndOutputs(
	Trace&			pppTrace,
	Network&		ionNet,
	ReceiverMap&	receiverMap,
	KFState&		kfState,
	bool			emptyEpoch,
	bool			inRts,
	bool			firstRtsEpoch)
{
	InteractiveTerminal::setMode(E_InteractiveMode::Outputs);

	string	_RTS;
	string	META_SUFFIX;

	if (inRts)
	{
		if (kfState.metaDataMap["SKIP_RTS_OUTPUT"] == "TRUE")
		{
			return;
		}

		_RTS		= "_RTS";
		META_SUFFIX	= SMOOTHED_SUFFIX;
	}

	//check whether we can write to the main state or need to make a copy (remember it will store in rts too)
	bool hold = false;

	if	( inRts == false
		&&acsConfig.ambrOpts.fix_and_hold)
	{
		hold = true;
	}

	auto time = kfState.time;

	static GTime clkOutputTime;
	static GTime obxOutputTime;
	static GTime sp3OutputTime;

	static bool firstEpoch = true;

	if (firstRtsEpoch)
	{
		//reset the first epoch things when starting rts
		firstEpoch = true;
	}

	if (firstEpoch)
	{
		//dont move above, rts resets these
		clkOutputTime = time.floorTime(acsConfig.clocks_output_interval);
		obxOutputTime = time.floorTime(acsConfig.orbex_output_interval);
		sp3OutputTime = time.floorTime(acsConfig.sp3_output_interval);

		firstEpoch = false;
	}

	tryPrepareFilterPointers(kfState, receiverMap);

	if (acsConfig.process_ppp)
	{
		mongoStates(kfState,
					{
						.suffix		= "/PPP" + _RTS,
						.instances	= acsConfig.mongoOpts.output_states,
						.queue		= acsConfig.mongoOpts.queue_outputs
					});

		kfState.outputStates(pppTrace, "/PPP" + _RTS);
	}

	nav.erp.filterValues = getErpFromFilter(kfState);

	if	(  acsConfig.ionModelOpts.model
		&& acsConfig.ssrOpts.atmosphere_sources.front() == +E_Source::KALMAN)
	{
		auto ionTrace = getTraceFile(ionNet);

		ionosphereSsrUpdate(ionTrace, kfState);
	}

	if (acsConfig.process_ionosphere)
	{
		auto ionTrace = getTraceFile(ionNet);

		obsIonoDataFromFilter(ionTrace, receiverMap, kfState);

		filterIonosphere(ionTrace, ionNet.kfState, receiverMap, time);

		if (acsConfig.ssrOpts.atmosphere_sources.front() == +E_Source::KALMAN)
		{
			ionosphereSsrUpdate(ionTrace, ionNet.kfState);
		}
	}

	KFState augmentedKF = kfState;

	if (acsConfig.process_ppp)
	{
		if	( acsConfig.reference_clock	!= "NO_REFERENCE"
			||acsConfig.reference_bias	!= "NO_REFERENCE")
		{
			augmentedKF = propagateUncertainty(pppTrace, kfState);

			augmentedKF.outputStates(pppTrace, "/PIVOT" + _RTS);

			mongoStates(augmentedKF,
						{
							.suffix		= "/PIVOT" + _RTS,
							.instances	= acsConfig.mongoOpts.output_states,
							.queue		= acsConfig.mongoOpts.queue_outputs
						});

			if (hold)
			{
				BOOST_LOG_TRIVIAL(error) << "Error: Ambiguity fix_and_hold requested but is not possible with pre-pivoted states";
				hold = false;
			}
		}

		if	(  acsConfig.process_minimum_constraints
			&& acsConfig.minconOpts.once_per_epoch)
		{
			BOOST_LOG_TRIVIAL(info) << " ------- PERFORMING MIN-CONSTRAINTS   --------" << "\n";

			for (auto& [id, rec] : receiverMap)
			{
				rec.minconApriori = rec.aprioriPos;
			}

			MinconStatistics minconStatistics;

			InteractiveTerminal minconTrace("MinimumConstraints", pppTrace);

			mincon(minconTrace, augmentedKF, &minconStatistics);				//todo aaron, orbits apriori need etting

			augmentedKF.outputStates(minconTrace, "/CONSTRAINED" + _RTS);

			outputMinconStatistics(minconTrace, minconStatistics);

			mongoStates(augmentedKF,
						{
							.suffix		= "/CONSTRAINED" + _RTS,
							.instances	= acsConfig.mongoOpts.output_states,
							.queue		= acsConfig.mongoOpts.queue_outputs
						});

			//erp values have probably changed due to mincon, update the nav vars before all the outputs
			//will have to revert this below because mincon is supposed to be temporary in once_per_epoch mode
			nav.erp.filterValues = getErpFromFilter(augmentedKF);

			if (hold)
			{
				BOOST_LOG_TRIVIAL(error) << "Error: Ambiguity fix_and_hold requested but is not possible with minimally constrained states";
				hold = false;
			}
		}

		bool arPossible = true;
		if	( inRts
			&&acsConfig.ambrOpts.fix_and_hold)
		{
			//was fixed on the forward run, dont do again
			arPossible = false;
		}

		if	(	arPossible
			&&	acsConfig.ambrOpts.mode
			&&	acsConfig.ambrOpts.once_per_epoch)
		{
			KFState* arState_ptr;

			if (hold)	{	arState_ptr = &kfState;			BOOST_LOG_TRIVIAL(info) << "Performing AR with fix and hold";	}
			else		{	arState_ptr = &augmentedKF;																		}

			auto& arState = *arState_ptr;

			fixAndHoldAmbiguities(pppTrace, arState);

			arState.outputStates(pppTrace, "/AR" + _RTS);

			mongoStates(arState,
						{
							.suffix		= "/AR" + _RTS,
							.instances	= acsConfig.mongoOpts.output_states,
							.queue		= acsConfig.mongoOpts.queue_outputs
						});

			//erp values have probably changed due to AR, update the nav vars before all the outputs
			//will have to revert this below because AR is supposed to be temporary in once_per_epoch mode
			nav.erp.filterValues = getErpFromFilter(arState);
		}

		for (auto& [id, rec] : receiverMap)
		{
			auto recTrace = getTraceFile(rec);
																			{	outputPppNmea(	recTrace,																	augmentedKF,	id);	}
			if (acsConfig.output_cost)										{	outputCost(					kfState.metaDataMap[COST_FILENAME_STR	+ id + META_SUFFIX],	augmentedKF,	rec);	}
			if (acsConfig.output_gpx)										{	writeGPX(					kfState.metaDataMap[GPX_FILENAME_STR	+ id + META_SUFFIX],	augmentedKF,	rec);	}
			if (acsConfig.output_pos)										{	writePOS(					kfState.metaDataMap[POS_FILENAME_STR	+ id + META_SUFFIX],	augmentedKF,	rec);	}
		}
	}

	if (1)
	{
		if (acsConfig.output_orbit_ics)										{	outputOrbitConfig(																						augmentedKF,			inRts);	}
		if (acsConfig.output_trop_sinex)									{	outputTropSinex(			kfState.metaDataMap[TROP_FILENAME_STR		+ META_SUFFIX],	time,			augmentedKF,	"MIX",	inRts);	}
		if (acsConfig.output_bias_sinex)									{	writeBiasSinex(	pppTrace,	kfState.metaDataMap[BSX_FILENAME_STR		+ META_SUFFIX],	time, 			augmentedKF,	ionNet.kfState,															receiverMap);	}
		if (acsConfig.output_clocks)		while (clkOutputTime <= time)	{	outputClocks(				kfState.metaDataMap[CLK_FILENAME_STR		+ META_SUFFIX],	clkOutputTime,	augmentedKF,	acsConfig.clocks_receiver_sources,	acsConfig.clocks_satellite_sources,	&receiverMap);							clkOutputTime += max(acsConfig.epoch_interval, acsConfig.clocks_output_interval);	}
		if (acsConfig.output_orbex)			while (obxOutputTime <= time)	{	outputOrbex(				kfState.metaDataMap[ORBEX_FILENAME_STR		+ META_SUFFIX],	obxOutputTime,	augmentedKF,	acsConfig.orbex_orbit_sources,		acsConfig.orbex_clock_sources,		acsConfig.orbex_attitude_sources);		obxOutputTime += max(acsConfig.epoch_interval, acsConfig.orbex_output_interval);	}
		if (acsConfig.output_sp3)			while (sp3OutputTime <= time)	{	outputSp3(					kfState.metaDataMap[SP3_FILENAME_STR		+ META_SUFFIX],	sp3OutputTime,	augmentedKF,	acsConfig.sp3_orbit_sources,		acsConfig.sp3_clock_sources,		emptyEpoch);							sp3OutputTime += max(acsConfig.epoch_interval, acsConfig.sp3_output_interval);		}
		if (acsConfig.output_erp)											{	writeErpFromNetwork(		kfState.metaDataMap[ERP_FILENAME_STR		+ META_SUFFIX],					augmentedKF)																												;	}
		if (acsConfig.output_ionstec)										{	writeIonStec(				kfState.metaDataMap[IONSTEC_FILENAME_STR	+ META_SUFFIX],					augmentedKF)																												;	}
		if (acsConfig.output_ionex)
		{
			auto ionTrace = getTraceFile(ionNet);

			if (acsConfig.process_ionosphere)								{	ionexFileWrite(	ionTrace,	kfState.metaDataMap[IONEX_FILENAME_STR		+ META_SUFFIX],	time,			ionNet.kfState);	}
			else															{	ionexFileWrite(	pppTrace,	kfState.metaDataMap[IONEX_FILENAME_STR		+ META_SUFFIX],	time,			augmentedKF);	}
		}
	}

	if (inRts == false)
	{
		if (acsConfig.output_rinex_nav)										{	writeRinexNav(																							acsConfig.rinex_nav_version);	}
		if (acsConfig.output_sbas_ems)										{	writeEMSdata(	pppTrace,	kfState.metaDataMap[EMS_FILENAME_STR]);		}

		mongoMeasSatStat		(receiverMap);
		outputApriori			(receiverMap);
		outputPredictedStates	(pppTrace, augmentedKF);
		prepareSsrStates		(pppTrace, augmentedKF, ionNet.kfState, time);

		//Only do rts if its not already in progress
		static double epochsPerRtsInterval	= acsConfig.pppOpts.rts_interval / acsConfig.epoch_interval;
		static double intervalRtsEpoch		= epochsPerRtsInterval;

		if	(  acsConfig.process_rts
			&& acsConfig.pppOpts.rts_interval
			&& epoch >= intervalRtsEpoch)
		{
			while (intervalRtsEpoch <= epoch)
			{
				intervalRtsEpoch += epochsPerRtsInterval;
			}

			rtsSmoothing(kfState, receiverMap, true);
		}

		outputStatistics(pppTrace, kfState.statisticsMap, kfState.statisticsMapSum);
	}

	//revert the erp filter values since we are done with the tempAugmentedKF
	nav.erp.filterValues = getErpFromFilter(kfState);
}
