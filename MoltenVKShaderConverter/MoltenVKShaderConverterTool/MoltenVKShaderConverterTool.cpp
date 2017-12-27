/*
 * MoltenVKShaderConverterTool.cpp
 *
 * Copyright (c) 2014-2017 The Brenwill Workshop Ltd. (http://www.brenwill.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "MoltenVKShaderConverterTool.h"
#include "FileSupport.h"
#include "GLSLToSPIRVConverter.h"
#include "SPIRVToMSLConverter.h"
#import <CoreFoundation/CFByteOrder.h>

using namespace std;
using namespace mvk;


/** The default list of vertex file extensions. */
static const char* _defaultVertexShaderExtns = "vs vsh vert vertex";

/** The default list of fragment file extensions. */
static const char* _defaultFragShaderExtns = "fs fsh frag fragment";

/** The default list of compute file extensions. */
static const char* _defaultCompShaderExtns = "cp cmp comp compute kn kl krn kern kernel";

/** The default list of SPIR-V file extensions. */
static const char* _defaultSPIRVShaderExtns = "spv spirv";


#pragma mark -
#pragma mark MoltenVKShaderConverterTool


int MoltenVKShaderConverterTool::run() {
	bool success = false;
	if ( !_directoryPath.empty() ) {
		string errMsg;
		success = iterateDirectory(_directoryPath, *this, _shouldUseDirectoryRecursion, errMsg);
		if ( !success ) { log(errMsg.data()); }
	} else {
		if (_shouldReadGLSL) {
			success = convertGLSL(_glslInFilePath, _spvOutFilePath, _mslOutFilePath, _shaderStage);
		} else if (_shouldReadSPIRV) {
			success = convertSPIRV(_spvInFilePath, _mslOutFilePath);
		} else {
			showUsage();
		}
	}
	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

bool MoltenVKShaderConverterTool::processFile(string filePath) {
	string absPath = absolutePath(filePath);
	string emptyPath;

	string pathExtn = pathExtension(absPath);
	if (_shouldReadGLSL && isGLSLFileExtension(pathExtn)) {
		convertGLSL(absPath, emptyPath, emptyPath, kMVKShaderStageAuto);
	} else if (_shouldReadSPIRV && isSPIRVFileExtension(pathExtn)) {
		convertSPIRV(absPath, emptyPath);
	}

	return false;
}

/**
 * Read GLSL code from a GLSL file, convert to SPIR-V, and optionally MSL,
 * and write the SPIR-V and/or MSL code to files.
 */
bool MoltenVKShaderConverterTool::convertGLSL(string& glslInFile,
											string& spvOutFile,
											string& mslOutFile,
											MVKShaderStage shaderStage) {
	string path;
	vector<char> fileContents;
	string glslCode;
	string errMsg;

	// Read the GLSL
	if (glslInFile.empty()) {
		log("The GLSL file to read from was not specified");
		return false;
	}

	path = glslInFile;
	if (readFile(path, fileContents, errMsg)) {
		string logMsg = "Read GLSL from file: " + lastPathComponent(path);
		log(logMsg.data());
	} else {
		errMsg = "Could not read GLSL file. " + errMsg;
		log(errMsg.data());
		return false;
	}
	glslCode.append(fileContents.begin(), fileContents.end());

	if (shaderStage == kMVKShaderStageAuto) {
		string pathExtn = pathExtension(glslInFile);
		shaderStage = shaderStageFromFileExtension(pathExtn);
	}
	if (shaderStage == kMVKShaderStageAuto) {
		errMsg = "Could not determine shader type from GLSL file: " + absolutePath(path);
		log(errMsg.data());
		return false;
	}

	// Convert GLSL to SPIR-V
	GLSLToSPIRVConverter glslConverter;
	glslConverter.setGLSL(glslCode);
	if (glslConverter.convert(shaderStage, _shouldLogConversions, _shouldLogConversions)) {
		if (_shouldLogConversions) { log(glslConverter.getResultLog().data()); }
	} else {
		string logMsg = "Could not convert GLSL in file: " + absolutePath(path);
		log(logMsg.data());
		log(glslConverter.getResultLog().data());
		return false;
	}

	const vector<uint32_t>& spv = glslConverter.getSPIRV();

	// Write the SPIR-V code to a file.
	// If no file has been supplied, create one from the GLSL file name.
	if (_shouldWriteSPIRV) {
		path = spvOutFile;
		if (path.empty()) { path = pathWithExtension(glslInFile, "spv", _shouldIncludeOrigPathExtn, _origPathExtnSep); }

		spirvToBytes(spv, fileContents);
		if (writeFile(path, fileContents, errMsg)) {
			string logMsg = "Saved SPIR-V to file: " + lastPathComponent(path);
			log(logMsg.data());
		} else {
			errMsg = "Could not write SPIR-V file. " + errMsg;
			log(errMsg.data());
			return false;
		}
	}

	return convertSPIRV(spv, glslInFile, mslOutFile, false);
}

/** Read SPIR-V code from a SPIR-V file, convert to MSL, and write the MSL code to files. */
bool MoltenVKShaderConverterTool::convertSPIRV(string& spvInFile,
											 string& mslOutFile) {
	string path;
	vector<char> fileContents;
	vector<uint32_t> spv;
	string errMsg;

	// Read the SPIRV
	if (spvInFile.empty()) {
		log("The SPIR-V file to read from was not specified");
		return false;
	}

	path = spvInFile;
	if (readFile(path, fileContents, errMsg)) {
		string logMsg = "Read SPIR-V from file: " + lastPathComponent(path);
		log(logMsg.data());
	} else {
		errMsg = "Could not read SPIR-V file. " + errMsg;
		log(errMsg.data());
		return false;
	}
	bytesToSPIRV(fileContents, spv);

	return convertSPIRV(spv, spvInFile, mslOutFile, _shouldLogConversions);
}

/** Read SPIR-V code from an array, convert to MSL, and write the MSL code to files. */
bool MoltenVKShaderConverterTool::convertSPIRV(const vector<uint32_t>& spv,
											 string& inFile,
											 string& mslOutFile,
											 bool shouldLogSPV) {
	if ( !_shouldWriteMSL ) { return true; }

	// Derive the context under which conversion will occur
	SPIRVToMSLConverterContext mslContext;
	mslContext.options.shouldFlipVertexY = _shouldFlipVertexY;

	SPIRVToMSLConverter spvConverter;
	spvConverter.setSPIRV(spv);
	if (spvConverter.convert(mslContext, shouldLogSPV, _shouldLogConversions, (_shouldLogConversions && shouldLogSPV))) {
		if (_shouldLogConversions) { log(spvConverter.getResultLog().data()); }
	} else {
		string errMsg = "Could not convert SPIR-V in file: " + absolutePath(inFile);
		log(errMsg.data());
		log(spvConverter.getResultLog().data());
		return false;
	}

	// Write the MSL to file
	string path = mslOutFile;
	if (mslOutFile.empty()) { path = pathWithExtension(inFile, "metal", _shouldIncludeOrigPathExtn, _origPathExtnSep); }
	const string& msl = spvConverter.getMSL();
	vector<char> fileContents;
	fileContents.insert(fileContents.end(), msl.begin(), msl.end());
	string errMsg;
	if (writeFile(path, fileContents, errMsg)) {
		string logMsg = "Saved MSL to file: " + lastPathComponent(path);
		log(logMsg.data());
		return true;
	} else {
		errMsg = "Could not write MSL file. " + errMsg;
		log(errMsg.data());
		return false;
	}
}

MVKShaderStage MoltenVKShaderConverterTool::shaderStageFromFileExtension(string& pathExtension) {
    for (auto& fx : _glslVtxFileExtns) { if (fx == pathExtension) { return kMVKShaderStageVertex; } }
    for (auto& fx : _glslFragFileExtns) { if (fx == pathExtension) { return kMVKShaderStageFragment; } }
    for (auto& fx : _glslCompFileExtns) { if (fx == pathExtension) { return kMVKShaderStageCompute; } }
	return kMVKShaderStageAuto;
}

bool MoltenVKShaderConverterTool::isGLSLFileExtension(string& pathExtension) {
    for (auto& fx : _glslVtxFileExtns) { if (fx == pathExtension) { return true; } }
    for (auto& fx : _glslFragFileExtns) { if (fx == pathExtension) { return true; } }
    for (auto& fx : _glslCompFileExtns) { if (fx == pathExtension) { return true; } }
	return false;
}

bool MoltenVKShaderConverterTool::isSPIRVFileExtension(string& pathExtension) {
    for (auto& fx : _spvFileExtns) { if (fx == pathExtension) { return true; } }
	return false;
}

/** Log the specified message to the console. */
void MoltenVKShaderConverterTool::log(const char* logMsg) { printf("%s\n", logMsg); }

/** Display usage information about this application on the console. */
void MoltenVKShaderConverterTool::showUsage() {
	string line = "\n\e[1m" + _processName + "\e[0m converts OpenGL Shading Language (GLSL) source code to";
	log((const char*)line.c_str());
	log("SPIR-V code, and/or to Metal Shading Language (MSL) source code, or converts");
	log("SPIR-V code to Metal Shading Language source code.");
	log("\nTo convert a single GLSL or SPIR-V file, include a file reference with the -gi");
	log("or -si option, respectively. To convert an entire directory of shader files,");
	log("use the -d option, along with the -gi or -si option. When using the -d option,");
	log("any file name supplied with the -gi or -si option will be ignored.");
    log("\nUse the -so or -mo option to indicate the desired type of output");
    log("(SPIR-V or MSL, respectively).");
	log("\nUsage:");
	log("  -d [\"dirPath\"]     - Path to a directory containing GLSL shader source code");
	log("                       files. The dirPath may be omitted to use the current");
	log("                       working directory.");
	log("  -r                 - (when using -d) Process directories recursively.");
	log("  -gi [\"glslInFile\"] - Indicates that GLSL shader code should be input.");
	log("                       The optional path parameter specifies the path to a");
	log("                       single file containing GLSL source code to be converted.");
	log("                       When using the -d option, the path parameter is ignored.");
	log("  -si [\"spvInFile\"]  - Indicates that SPIR-V shader code should be input.");
	log("                       The optional path parameter specifies the path to a");
	log("                       single file containing SPIR-V code to be converted.");
	log("                       When using the -d option, the path parameter is ignored.");
	log("  -so [\"spvOutFile\"] - Indicates that SPIR-V shader code should be output.");
	log("                       The optional path parameter specifies the path to a single");
	log("                       file to contain the SPIR-V code. When using the -d option,");
	log("                       the path parameter is ignored.");
	log("  -mo [\"mslOutFile\"] - Indicates that MSL shader source code should be output.");
	log("                       The optional path parameter specifies the path to a single");
	log("                       file to contain the MSL code. When using the -d option,");
	log("                       the path parameter is ignored.");
	log("  -t shaderType      - Shader type: vertex or fragment. Must be one of v, f, or c.");
	log("                       May be omitted to auto-detect.");
	log("  -c                 - Combine the GLSL and converted Metal Shader source code");
	log("                       into a single ouput file.");
	log("  -Iv                - Disable inversion of the vertex coordinate Y-axis");
    log("                       (default is to invert vertex coordinates).");
	log("  -xs \"xtnSep\"       - Separator to use when including file extension of original");
	log("                       code file name in derived converted code file name.");
	log("                       Default is \"_\" (myshdr.vsh -> myshdr_vsh.metal).");
	log("  -XS                - Disable including file extension of original code");
	log("                       file name in derived converted code file name");
	log("                       (myshdr.vsh -> myshdr.metal).");
	log("  -vx \"fileExtns\"    - List of GLSL vertex shader file extensions.");
	log("                       May be omitted for defaults (\"vs vsh vert vertex\").");
	log("  -fx \"fileExtns\"    - List of GLSL fragment shader file extensions.");
	log("                       May be omitted for defaults (\"fs fsh frag fragment\").");
    log("  -cx \"fileExtns\"    - List of GLSL compute shader file extensions.");
    log("                       May be omitted for defaults (\"cp cmp comp compute kn kl krn kern kernel\").");
	log("  -sx \"fileExtns\"    - List of SPIR-V shader file extensions.");
	log("                       May be omitted for defaults (\"spv spirv\").");
	log("  -l                 - Log the conversion results to the console (to aid debugging).");
	log("");
}


#pragma mark Construction

MoltenVKShaderConverterTool::MoltenVKShaderConverterTool(int argc, const char* argv[]) {
	extractTokens(_defaultVertexShaderExtns, _glslVtxFileExtns);
	extractTokens(_defaultFragShaderExtns, _glslFragFileExtns);
    extractTokens(_defaultCompShaderExtns, _glslCompFileExtns);
	extractTokens(_defaultSPIRVShaderExtns, _spvFileExtns);
	_origPathExtnSep = "_";
	_shaderStage = kMVKShaderStageAuto;
	_isActive = false;
	_shouldUseDirectoryRecursion = false;
	_shouldReadGLSL = false;
	_shouldReadSPIRV = false;
	_shouldWriteSPIRV = false;
	_shouldWriteMSL = false;
	_shouldCombineGLSLAndMSL = false;
    _shouldFlipVertexY = true;
	_shouldIncludeOrigPathExtn = true;
	_shouldLogConversions = false;

	_isActive = parseArgs(argc, argv);
	if ( !_isActive ) { showUsage(); }
}

bool MoltenVKShaderConverterTool::parseArgs(int argc, const char* argv[]) {
	if (argc == 0) { return false; }

	string execPath(argv[0]);
	_processName = lastPathComponent(execPath);

	for (int argIdx = 1; argIdx < argc; argIdx++) {
		string arg = argv[argIdx];

		if ( !isOptionArg(arg) ) { return false; }

		if (equal(arg, "-d", false)) {
			int optIdx = argIdx;
			argIdx = optionParam(_directoryPath, argIdx, argc, argv);
			if (argIdx == optIdx) { return false; }
			_directoryPath = absolutePath(_directoryPath);
			continue;
		}

		if(equal(arg, "-r", true)) {
			_shouldUseDirectoryRecursion = true;
			continue;
		}

		if (equal(arg, "-gi", true)) {
			_shouldReadGLSL = true;
			argIdx = optionParam(_glslInFilePath, argIdx, argc, argv);
			continue;
		}

		if (equal(arg, "-si", true)) {
			_shouldReadSPIRV = true;
			argIdx = optionParam(_spvInFilePath, argIdx, argc, argv);
			continue;
		}

		if (equal(arg, "-so", true)) {
			_shouldWriteSPIRV = true;
			argIdx = optionParam(_spvOutFilePath, argIdx, argc, argv);
			continue;
		}

		if (equal(arg, "-mo", true)) {
			_shouldWriteMSL = true;
			argIdx = optionParam(_mslOutFilePath, argIdx, argc, argv);
			continue;
		}

		if (equal(arg, "-t", true)) {
			int optIdx = argIdx;
			string shdrTypeStr;
			argIdx = optionParam(shdrTypeStr, argIdx, argc, argv);
			if (argIdx == optIdx || shdrTypeStr.length() == 0) { return false; }

			switch (shdrTypeStr.front()) {
				case 'v':
					_shaderStage = kMVKShaderStageVertex;
					break;
				case 'f':
					_shaderStage = kMVKShaderStageFragment;
					break;
				case 'c':
					_shaderStage = kMVKShaderStageCompute;
					break;
				default:
					break;
			}
			continue;
		}

		if(equal(arg, "-c", true)) {
			_shouldCombineGLSLAndMSL = true;
			continue;
		}

        if(equal(arg, "-Iv", true)) {
            _shouldFlipVertexY = false;
            continue;
        }

		if (equal(arg, "-xs", true)) {
			_shouldIncludeOrigPathExtn = true;
			argIdx++;
			if (argIdx < argc) { _origPathExtnSep = argv[argIdx]; }
			continue;
		}

		if(equal(arg, "-XS", true)) {
			_shouldIncludeOrigPathExtn = false;
			continue;
		}

		if (equal(arg, "-vx", true)) {
			int optIdx = argIdx;
			string shdrExtnStr;
			argIdx = optionParam(shdrExtnStr, argIdx, argc, argv);
			if (argIdx == optIdx || shdrExtnStr.length() == 0) { return false; }
			extractTokens(shdrExtnStr, _glslVtxFileExtns);
			continue;
		}

		if (equal(arg, "-fx", true)) {
			int optIdx = argIdx;
			string shdrExtnStr;
			argIdx = optionParam(shdrExtnStr, argIdx, argc, argv);
			if (argIdx == optIdx || shdrExtnStr.length() == 0) { return false; }
			extractTokens(shdrExtnStr, _glslFragFileExtns);
			continue;
		}

        if (equal(arg, "-cx", true)) {
            int optIdx = argIdx;
            string shdrExtnStr;
            argIdx = optionParam(shdrExtnStr, argIdx, argc, argv);
            if (argIdx == optIdx || shdrExtnStr.length() == 0) { return false; }
            extractTokens(shdrExtnStr, _glslCompFileExtns);
            continue;
        }

		if (equal(arg, "-sx", true)) {
			int optIdx = argIdx;
			string shdrExtnStr;
			argIdx = optionParam(shdrExtnStr, argIdx, argc, argv);
			if (argIdx == optIdx || shdrExtnStr.length() == 0) { return false; }
			extractTokens(shdrExtnStr, _spvFileExtns);
			continue;
		}

		if(equal(arg, "-l", true)) {
			_shouldLogConversions = true;
			continue;
		}

	}

	return true;
}

/** Returns whether the specified command line arg is an option arg. */
bool MoltenVKShaderConverterTool::isOptionArg(string& arg) {
	return (arg.length() > 1 && arg.front() == '-');
}

/** 
 * Sets the contents of the specified string to the parameter part of the option at the 
 * specified arg index, and increments and returns the option index. If no parameter was
 * provided for the option, the string will be set to an empty string, and the returned
 * index will be the same as the specified index.
 */
int MoltenVKShaderConverterTool::optionParam(string& optionParamResult,
										   int optionArgIndex,
										   int argc,
										   const char* argv[]) {
	int optParamIdx = optionArgIndex + 1;
	if (optParamIdx < argc) {
		string arg(argv[optParamIdx]);
		if ( !isOptionArg(arg) ) {
			optionParamResult = arg;
			return optParamIdx;
		}
	}
	optionParamResult.clear();
	return optionArgIndex;
}


#pragma mark -
#pragma mark Support functions

/** Template function for tokenizing the components of a string into a vector. */
template <typename Container>
Container& split(Container& result,
				 const typename Container::value_type& s,
				 const typename Container::value_type& delimiters,
				 bool includeEmptyElements) {
	result.clear();
	size_t current;
	size_t next = -1;
	do {
		if (includeEmptyElements) {
			next = s.find_first_not_of( delimiters, next + 1 );
			if (next == Container::value_type::npos) break;
			next -= 1;
		}
		current = next + 1;
		next = s.find_first_of( delimiters, current );
		result.push_back( s.substr( current, next - current ) );
	} while (next != Container::value_type::npos);
	return result;
}

void mvk::extractTokens(string str, vector<string>& tokens) {
	split(tokens, str, " \t\n\f", false);
}

/** Compares the specified characters ignoring case. */
static bool compareIgnoringCase(unsigned char a, unsigned char b) {
	return tolower(a) == tolower(b);
}

bool mvk::equal(string const& a, string const& b, bool checkCase) {
	if (a.length() != b.length()) { return false; }
	return checkCase ? (a == b) : (equal(b.begin(), b.end(), a.begin(), compareIgnoringCase));
}

void mvk::spirvToBytes(const vector<uint32_t>& spv, vector<char>& bytes) {
	// Assumes desired endianness.
	size_t byteCnt = spv.size() * sizeof(uint32_t);
	char* cBytes = (char*)spv.data();
	bytes.clear();
	bytes.insert(bytes.end(), cBytes, cBytes + byteCnt);
}

void mvk::bytesToSPIRV(const vector<char>& bytes, vector<uint32_t>& spv) {
	size_t spvCnt = bytes.size() / sizeof(uint32_t);
	uint32_t* cSPV = (uint32_t*)bytes.data();
	spv.clear();
	spv.insert(spv.end(), cSPV, cSPV + spvCnt);
	ensureSPIRVEndianness(spv);
}

bool mvk::ensureSPIRVEndianness(vector<uint32_t>& spv) {
	if (spv.empty()) { return false; }					// Nothing to convert

	uint32_t magNum = spv.front();
	if (magNum == spv::MagicNumber) { return false; }	// No need to convert

	if (CFSwapInt32(magNum) == spv::MagicNumber) {		// Yep, it's SPIR-V, but wrong endianness
        for (auto& elem : spv) { elem = CFSwapInt32(elem); }
		return true;
	}
	return false;		// Not SPIR-V, so don't convert
}

