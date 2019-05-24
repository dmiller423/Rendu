//
//  Config.cpp
//
//  Created by Simon Rodriguez on 30/12/2017.
//  Copyright © 2017 Simon Rodriguez. All rights reserved.
//

#include "Config.hpp"
#include "resources/ResourcesManager.hpp"

#include <sstream>

Config::Config(const std::vector<std::string> & argv){
	
	if(argv.size() < 2){
		// Nothing to do, keep using default values.
		return;
	}
	
	// Have we received a config file as argument?
	const std::string potentialConfig = TextUtilities::trim(argv[1], "-");
	
	if(potentialConfig == "c" || potentialConfig == "config"){
		// Safety check.
		if(argv.size() < 3){
			Log::Error() << Log::Config << "Missing path for --config argument. Using default config." << std::endl;
			return;
		}
		parseFromFile(argv[2], _rawArguments);
	} else {
		// Directly parse arguments.
		parseFromArgs(argv, _rawArguments);
	}
	
	// Extract logging settings;
	std::string logPath;
	bool logVerbose = false;
	
	for(const auto & arg : _rawArguments){
		const std::string key = arg.first;
		const std::vector<std::string> & values = arg.second;
		
		if(key == "verbose"){
			logVerbose = true;
		} else if(key == "log-path"){
			logPath = values[0];
		}
	}
	
	if(!logPath.empty()){
		Log::setDefaultFile(logPath);
	}
	Log::setDefaultVerbose(logVerbose);
	
}


void Config::parseFromFile(const std::string & filePath, std::map<std::string, std::vector<std::string>> & arguments){
	// Load config from given file.
	const std::string configContent = Resources::loadStringFromExternalFile(filePath);
	if(configContent.empty()){
		Log::Error() << Log::Config << "Missing/empty config file. Using default config." << std::endl;
		return;
	}
	std::istringstream lines(configContent);
	std::string line;
	while(std::getline(lines,line)){
		// Clean line.
		const std::string lineClean = TextUtilities::trim(line, " ");
		if(lineClean.empty()) {
			continue;
		}
		// Split at first space.
		const std::string::size_type spacePos = lineClean.find_first_of(" ");
		std::vector<std::string> values;
		std::string firstArg = "";
		if(spacePos == std::string::npos){
			// This is a on/off argument.
			firstArg = TextUtilities::trim(lineClean, "-");
		} else {
			// We need to split the whole line.
			firstArg = TextUtilities::trim(lineClean.substr(0, spacePos), "-");
			
			std::string::size_type beginPos = spacePos+1;
			std::string::size_type afterEndPos = lineClean.find_first_of(" ", beginPos);
			while (afterEndPos != std::string::npos) {
				const std::string value = lineClean.substr(beginPos, afterEndPos - beginPos);
				values.push_back(value);
				beginPos = afterEndPos + 1;
				afterEndPos = lineClean.find_first_of(" ", beginPos);
			}
			// There is one remaining value, the last one.
			const std::string value = lineClean.substr(beginPos);
			values.push_back(value);
			
		}
		if(!firstArg.empty()) {
			arguments[firstArg] = values;
		}
	}
}


void Config::parseFromArgs(const std::vector<std::string> & argv, std::map<std::string, std::vector<std::string>> & arguments){
	for(size_t argi = 1; argi < argv.size(); ){
		// Clean the argument from any -
		const std::string firstArg = TextUtilities::trim(argv[argi], "-");
		if (firstArg.empty()) {
			continue;
		}
		std::vector<std::string> values;
		++argi;
		// While we do not encounter a dash, the values are associated to the current argument.
		while (argi < argv.size() && argv[argi][0] != '-') {
			values.emplace_back(argv[argi]);
			++argi;
		}
		arguments[firstArg] = values;

	}
}

RenderingConfig::RenderingConfig(const std::vector<std::string> & argv) : Config(argv){
	processArguments();
}


void RenderingConfig::processArguments(){
	
	for(const auto & arg : _rawArguments){
		const std::string key = arg.first;
		const std::vector<std::string> & values = arg.second;
		
		if(key == "novsync" || key == "no-vsync"){
			vsync = false;
		} else if(key == "half-rate"){
			rate = 30;
		} else if(key == "fullscreen"){
			fullscreen = true;
		} else if(key == "internal-res" || key == "ivr"){
			internalVerticalResolution = std::stoi(values[0]);
		} else if(key == "wxh"){
			const unsigned int w = (unsigned int)std::stoi(values[0]);
			const unsigned int h = (unsigned int)std::stoi(values[1]);
			initialWidth = w;
			initialHeight = h;
		} else if(key == "force-aspect-ratio" || key == "far"){
			forceAspectRatio = true;
		}
	}
}



