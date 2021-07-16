#include "ModuleManager.hpp"

#include <string.h>

#include <cstring>

#include "Build.hpp"
#include "Converters.hpp"
#include "DynamicLoader.hpp"
#include "Evaluator.hpp"
#include "EvaluatorEnums.hpp"
#include "FileUtilities.hpp"
#include "GeneratorHelpers.hpp"
#include "Generators.hpp"
#include "Logging.hpp"
#include "OutputPreambles.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

// The ' symbols tell the signature validator that the actual contents of those symbols can be
// user-defined (just like C letting you specify arguments without names)
const char* g_modulePreBuildHookSignature =
    "('manager (& ModuleManager) 'module (* Module) &return bool)";

void listBuiltInGenerators()
{
	EvaluatorEnvironment environment;
	importFundamentalGenerators(environment);
	for (GeneratorIterator it = environment.generators.begin(); it != environment.generators.end();
	     ++it)
	{
		Logf("  %s\n", it->first.c_str());
	}
	environmentDestroyInvalidateTokens(environment);
}

void moduleManagerInitialize(ModuleManager& manager)
{
	importFundamentalGenerators(manager.environment);

	// Create module definition for top-level references to attach to
	// The token isn't actually tied to one file
	manager.globalPseudoInvocationName = {
	    TokenType_Symbol, globalDefinitionName, "global_pseudotarget", 1, 0, 1};
	{
		ObjectDefinition moduleDefinition = {};
		moduleDefinition.name = manager.globalPseudoInvocationName.contents;
		moduleDefinition.definitionInvocation = &manager.globalPseudoInvocationName;
		moduleDefinition.type = ObjectType_PseudoObject;
		moduleDefinition.isRequired = true;

		// The context on the definition shouldn't be used in this case unless this definition is
		// going to be reevaluated or replaced, which doesn't make much sense. Let's put stuff in
		// anyways, just to be sure
		{
			EvaluatorContext moduleContext = {};
			moduleContext.scope = EvaluatorScope_Module;
			moduleContext.definitionName = &manager.globalPseudoInvocationName;
			moduleContext.isRequired = true;
			moduleDefinition.context = moduleContext;
		}

		// Will be cleaned up when the environment is destroyed
		GeneratorOutput* compTimeOutput = new GeneratorOutput;
		moduleDefinition.output = compTimeOutput;
		if (!addObjectDefinition(manager.environment, moduleDefinition))
			Log("error: <global> couldn't be added. Was module manager initialized twice? Things "
			    "will definitely break\n");
	}

	manager.environment.moduleManager = &manager;

	// Command defaults
	{
#ifdef WINDOWS
		manager.environment.isMsvcCompiler = true;

		// MSVC by default
		// Our lives could be easier by using Clang or MinGW, but it wouldn't be the ideal for
		// hardcore Windows users, who we should support
		manager.environment.compileTimeBuildCommand.fileToExecute = "cl.exe";
		manager.environment.compileTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    // Not 100% sure what the right default for this is
		    {ProcessCommandArgumentType_String, "/EHsc"},
		    // Need this to properly add declspec for importing symbols (on Linux, we don't need
		    // declspec, so it's ifdef'd based on platform)
		    {ProcessCommandArgumentType_String, "/DWINDOWS"},
		    // Need to use dynamic runtime so everything is shared. Cakelisp itself must be built
		    // with this matching as well (use just /MD for release) See
		    // https://stackoverflow.com/questions/22279052/c-passing-stdstring-by-reference-to-function-in-dll
		    {ProcessCommandArgumentType_String, "/MDd"},
		    // Debug only
		    {ProcessCommandArgumentType_String, "/DEBUG:FASTLINK"},
		    {ProcessCommandArgumentType_String, "/Zi"},
		    {ProcessCommandArgumentType_String, "/c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_DebugSymbolsOutput, EmptyString},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, EmptyString}};

		manager.environment.compileTimeLinkCommand.fileToExecute = "link.exe";
		manager.environment.compileTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_String, "/DLL"},
		    // On Windows, .exes create .lib files for exports. Link it here so we don't get
		    // unresolved externals
		    {ProcessCommandArgumentType_String, "/LIBPATH:bin"},
		    {ProcessCommandArgumentType_String, "cakelisp.lib"},
		    {ProcessCommandArgumentType_ImportLibraryPaths, EmptyString},
		    {ProcessCommandArgumentType_ImportLibraries, EmptyString},
		    // Debug only
		    {ProcessCommandArgumentType_String, "/DEBUG:FASTLINK"},
		    {ProcessCommandArgumentType_DynamicLibraryOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};

		// TODO Precompiled headers on windows. See
		// https://docs.microsoft.com/en-us/cpp/build/creating-precompiled-header-files?view=msvc-160
		// https://docs.microsoft.com/en-us/cpp/build/reference/yc-create-precompiled-header-file?view=msvc-160
		// https://docs.microsoft.com/en-us/cpp/build/reference/yu-use-precompiled-header-file?view=msvc-160
		manager.environment.compileTimeHeaderPrecompilerCommand.fileToExecute = "cl.exe";
		manager.environment.compileTimeHeaderPrecompilerCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_String, "/EHsc"},
		    {ProcessCommandArgumentType_String, "/c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_DebugSymbolsOutput, EmptyString},
		    {ProcessCommandArgumentType_IncludeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString}};

		manager.environment.buildTimeBuildCommand.fileToExecute = "cl.exe";
		manager.environment.buildTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_String, "/EHsc"},
		    {ProcessCommandArgumentType_String, "/c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_DebugSymbolsOutput, EmptyString},
		    {ProcessCommandArgumentType_IncludeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString}};

		manager.environment.buildTimeLinkCommand.fileToExecute = "link.exe";
		manager.environment.buildTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString},
		    {ProcessCommandArgumentType_ExecutableOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString},
		    {ProcessCommandArgumentType_LibrarySearchDirs, EmptyString},
		    {ProcessCommandArgumentType_Libraries, EmptyString},
		    {ProcessCommandArgumentType_LibraryRuntimeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_LinkerArguments, EmptyString}};
#else
		// 13.2 seconds Debug; 10.25 no debug
		// manager.environment.comptimeUsePrecompiledHeaders = false;
		// 7.37 seconds (including building pch, 6.21 w/o); 3.728s no debug (excluding pch; if build
		// pch, 4.62s)
		manager.environment.comptimeUsePrecompiledHeaders = true;

		// G++ by default, because most distros seem to have it over clang
		const char* defaultCompilerLinker = "g++";  // 9s
		// const char* defaultCompilerLinker = "clang++"; // 11.9s

		manager.environment.compileTimeBuildCommand.fileToExecute = defaultCompilerLinker;
		manager.environment.compileTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
			{ProcessCommandArgumentType_String, "-std=c++11"},
			{ProcessCommandArgumentType_String, "-Wc++11-extensions"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, EmptyString},
		    {ProcessCommandArgumentType_PrecompiledHeaderInclude, EmptyString},
		    {ProcessCommandArgumentType_String, "-fPIC"}};

		manager.environment.compileTimeLinkCommand.fileToExecute = defaultCompilerLinker;
		manager.environment.compileTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-shared"},
			{ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_DynamicLibraryOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Build.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Converters.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/DynamicLoader.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Evaluator.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/FileUtilities.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/GeneratorHelpers.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Generators.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Logging.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/ModuleManager.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/OutputPreambles.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/RunProcess.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Tokenizer.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Utilities.cpp.o"},
			{ProcessCommandArgumentType_String, "cakelisp_cache/Bootstrap/Writer.cpp.o"}
			};

		// Note that this command must match the compilation command to be compatible, see
		// https://gcc.gnu.org/onlinedocs/gcc/Precompiled-Headers.html
		manager.environment.compileTimeHeaderPrecompilerCommand.fileToExecute =
		    defaultCompilerLinker;
		manager.environment.compileTimeHeaderPrecompilerCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-x"},
		    {ProcessCommandArgumentType_String, "c++-header"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_PrecompiledHeaderOutput, EmptyString},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, EmptyString},
		    {ProcessCommandArgumentType_String, "-fPIC"}};

		manager.environment.buildTimeBuildCommand.fileToExecute = defaultCompilerLinker;
		manager.environment.buildTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    // Probably unnecessary to make the user's code position-independent, but it does make
		    // hotreloading a bit easier to try out
		    {ProcessCommandArgumentType_String, "-fPIC"},
		    {ProcessCommandArgumentType_IncludeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString}};

		manager.environment.buildTimeLinkCommand.fileToExecute = defaultCompilerLinker;
		manager.environment.buildTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ExecutableOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString},
		    {ProcessCommandArgumentType_LibrarySearchDirs, EmptyString},
		    {ProcessCommandArgumentType_Libraries, EmptyString},
		    {ProcessCommandArgumentType_LibraryRuntimeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_LinkerArguments, EmptyString}};
#endif
	}

	manager.environment.useCachedFiles = true;
	makeDirectory(cakelispWorkingDir);
	if (logging.fileSystem || logging.phases)
		Logf("Using cache at %s\n", cakelispWorkingDir);

	// By always searching relative to CWD, any subsequent imports with the module filename will
	// resolve correctly
	manager.environment.searchPaths.push_back(".");
}

void moduleManagerDestroyKeepDynLibs(ModuleManager& manager)
{
	environmentDestroyInvalidateTokens(manager.environment);
	for (Module* module : manager.modules)
	{
		delete module->tokens;
		delete module->generatedOutput;
		free((void*)module->filename);
		delete module;
	}
	manager.modules.clear();
}

void moduleManagerDestroy(ModuleManager& manager)
{
	moduleManagerDestroyKeepDynLibs(manager);
	closeAllDynamicLibraries();
}

void makeSafeFilename(char* buffer, int bufferSize, const char* filename)
{
	char* bufferWrite = buffer;
	for (const char* currentChar = filename; *currentChar; ++currentChar)
	{
		if (*currentChar == '\\')
			*bufferWrite = '/';
		else
			*bufferWrite = *currentChar;

		++bufferWrite;
		if (bufferWrite - buffer >= bufferSize)
		{
			Log("error: could not make safe filename: buffer too small\n");
			break;
		}
	}
}

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut)
{
	*tokensOut = nullptr;

	FILE* file = fileOpen(filename, "r");
	if (!file)
		return false;

	char lineBuffer[2048] = {0};
	int lineNumber = 1;
	// We need to be very careful about when we delete this so as to not invalidate pointers
	// It is immutable to also disallow any pointer invalidation if we were to resize it
	const std::vector<Token>* tokens = nullptr;
	{
		std::vector<Token>* tokens_CREATIONONLY = new std::vector<Token>;
		bool isFirstLine = true;
		while (fgets(lineBuffer, sizeof(lineBuffer), file))
		{
			if (logging.tokenization)
				Logf("%s", lineBuffer);

			// Check for shebang and ignore this line if found. This allows users to execute their
			// scripts via e.g. ./MyScript.cake, given #!/usr/bin/cakelisp --execute
			if (isFirstLine)
			{
				isFirstLine = false;
				if (lineBuffer[0] == '#' && lineBuffer[1] == '!')
				{
					if (logging.tokenization)
						Log("Skipping shebang\n");

					++lineNumber;
					continue;
				}
			}

			const char* error =
			    tokenizeLine(lineBuffer, filename, lineNumber, *tokens_CREATIONONLY);
			if (error != nullptr)
			{
				Logf("%s:%d: error: %s\n", filename, lineNumber, error);

				delete tokens_CREATIONONLY;
				return false;
			}

			++lineNumber;
		}

		// Make it const to avoid pointer invalidation due to resize
		tokens = tokens_CREATIONONLY;
	}

	if (logging.tokenization)
		Logf("Tokenized %d lines\n", lineNumber - 1);

	if (tokens->empty())
	{
		Logf(
		    "error: empty file or tokenization error with '%s'. Please remove from system, or add "
		    "(ignore)\n",
		    filename);
		delete tokens;
		return false;
	}

	if (!validateTokens(*tokens))
	{
		delete tokens;
		return false;
	}

	if (logging.tokenization)
	{
		Log("\nResult:\n");

		// No need to validate, we already know it's safe
		int nestingDepth = 0;
		for (const Token& token : *tokens)
		{
			printIndentToDepth(nestingDepth);

			Logf("%s", tokenTypeToString(token.type));

			bool printRanges = true;
			if (printRanges)
			{
				Logf("\t\tline %d, from line character %d to %d\n", token.lineNumber,
				     token.columnStart, token.columnEnd);
			}

			if (token.type == TokenType_OpenParen)
			{
				++nestingDepth;
			}
			else if (token.type == TokenType_CloseParen)
			{
				--nestingDepth;
			}

			if (!token.contents.empty())
			{
				printIndentToDepth(nestingDepth);
				Logf("\t%s\n", token.contents.c_str());
			}
		}
	}

	fclose(file);

	*tokensOut = tokens;

	return true;
}

bool moduleManagerAddEvaluateFile(ModuleManager& manager, const char* filename, Module** moduleOut)
{
	if (moduleOut)
		*moduleOut = nullptr;

	if (!filename)
		return false;

	char resolvedPath[MAX_PATH_LENGTH] = {0};
	makeAbsoluteOrRelativeToWorkingDir(filename, resolvedPath, ArraySize(resolvedPath));
	char safePathBuffer[MAX_PATH_LENGTH] = {0};
	makeSafeFilename(safePathBuffer, sizeof(safePathBuffer), resolvedPath);

	const char* normalizedFilename = StrDuplicate(safePathBuffer);
	// Enabling this makes all file:line messages really long. For now, I'll keep it as relative to
	// current working directory of this executable.
	// const char* normalizedFilename = makeAbsolutePath_Allocated(".", filename);
	if (!normalizedFilename)
	{
#ifdef UNIX
		perror("failed to normalize filename: ");
#else
		Logf("error: could not normalize filename, or file not found: %s\n", filename);
#endif
		return false;
	}

	// Check for already loaded module. Make sure to use absolute paths to protect the user from
	// multiple includes in case they got tricky with their import path
	for (Module* module : manager.modules)
	{
		const char* normalizedProspectiveModuleFilename = makeAbsolutePath_Allocated(".", filename);
		if (!normalizedProspectiveModuleFilename)
		{
			Logf("error: failed to normalize path %s\n", filename);
			free((void*)normalizedFilename);
			return false;
		}

		const char* normalizedModuleFilename = makeAbsolutePath_Allocated(".", module->filename);

		if (!normalizedModuleFilename)
		{
			Logf("error: failed to normalize path %s\n", module->filename);
			free((void*)normalizedProspectiveModuleFilename);
			free((void*)normalizedFilename);
			return false;
		}

		if (strcmp(normalizedModuleFilename, normalizedProspectiveModuleFilename) == 0)
		{
			if (moduleOut)
				*moduleOut = module;

			if (logging.imports)
				Logf("Already loaded %s\n", normalizedFilename);
			free((void*)normalizedFilename);
			free((void*)normalizedProspectiveModuleFilename);
			free((void*)normalizedModuleFilename);
			return true;
		}

		free((void*)normalizedProspectiveModuleFilename);
		free((void*)normalizedModuleFilename);
	}

	Module* newModule = new Module();
	// We need to keep this memory around for the lifetime of the token, regardless of relocation
	newModule->filename = normalizedFilename;
	// This stage cleans up after itself if it fails
	if (!moduleLoadTokenizeValidate(newModule->filename, &newModule->tokens))
	{
		Logf("error: failed to tokenize %s\n", newModule->filename);
		delete newModule;
		free((void*)normalizedFilename);
		return false;
	}

	newModule->generatedOutput = new GeneratorOutput;

	manager.modules.push_back(newModule);

	EvaluatorContext moduleContext = {};
	moduleContext.module = newModule;
	moduleContext.scope = EvaluatorScope_Module;
	moduleContext.definitionName = &manager.globalPseudoInvocationName;
	// Module always requires all its functions
	// TODO: Local functions can be left out if not referenced (in fact, they may warn in C if not)
	moduleContext.isRequired = true;
	// A delimiter isn't strictly necessary here, but it is nice to space out things
	StringOutput moduleDelimiterTemplate = {};
	moduleDelimiterTemplate.modifiers = StringOutMod_NewlineAfter;
	moduleContext.delimiterTemplate = moduleDelimiterTemplate;
	int numErrors =
	    EvaluateGenerateAll_Recursive(manager.environment, moduleContext, *newModule->tokens,
	                                  /*startTokenIndex=*/0, *newModule->generatedOutput);
	// After this point, the module may have references to its tokens in the environmment, so we
	// cannot destroy it until we're done evaluating everything
	if (numErrors)
	{
		Logf("error: failed to evaluate %s\n", newModule->filename);
		return false;
	}

	if (moduleOut)
		*moduleOut = newModule;

	if (logging.imports)
		Logf("Loaded %s\n", newModule->filename);
	return true;
}

bool moduleManagerEvaluateResolveReferences(ModuleManager& manager)
{
	return EvaluateResolveReferences(manager.environment);
}

// Directory is named from build configuration labels, e.g. Debug-HotReload
// Order DOES matter, in case changing configuration order changes which settings get eval'd first
static bool createBuildOutputDirectory(EvaluatorEnvironment& environment, std::string& outputDirOut)
{
	// As soon as we start writing, we need to decide what directory we will write to. Fix build
	// configuration labels because it's too late to change them now
	environment.buildConfigurationLabelsAreFinal = true;

	// Sane default in case something goes wrong
	outputDirOut = cakelispWorkingDir;

	char outputDirName[MAX_PATH_LENGTH] = {0};
	int numLabels = (int)environment.buildConfigurationLabels.size();
	char* writeHead = outputDirName;

	if (!writeStringToBuffer(cakelispWorkingDir, &writeHead, outputDirName, sizeof(outputDirName)))
	{
		Log("error: ran out of space writing build configuration output directory name\n");
		return false;
	}
	if (!writeCharToBuffer('/', &writeHead, outputDirName, sizeof(outputDirName)))
	{
		Log("error: ran out of space writing build configuration output directory name\n");
		return false;
	}

	for (int i = 0; i < numLabels; ++i)
	{
		const std::string& label = environment.buildConfigurationLabels[i];
		if (!writeStringToBuffer(label.c_str(), &writeHead, outputDirName, sizeof(outputDirName)))
		{
			Log("error: ran out of space writing build configuration output directory name\n");
			break;
		}

		// Delimiter
		if (i < numLabels - 1)
		{
			if (!writeCharToBuffer('-', &writeHead, outputDirName, sizeof(outputDirName)))
			{
				Log("error: ran out of space writing build configuration output directory name\n");
				break;
			}
		}
	}

	if (numLabels == 0)
		PrintfBuffer(outputDirName, "%s/default", cakelispWorkingDir);

	makeDirectory(outputDirName);

	if (logging.fileSystem || logging.phases)
		Logf("Outputting artifacts to %s\n", outputDirName);

	outputDirOut = outputDirName;

	return true;
}

bool moduleManagerWriteGeneratedOutput(ModuleManager& manager)
{
	createBuildOutputDirectory(manager.environment, manager.buildOutputDir);

	NameStyleSettings nameSettings;
	WriterFormatSettings formatSettings;

	for (Module* module : manager.modules)
	{
		WriterOutputSettings outputSettings;
		outputSettings.sourceCakelispFilename = module->filename;

		GeneratorOutput header;
		GeneratorOutput footer;
		// Something to attach the reason for generating this output
		const Token* blameToken = &(*module->tokens)[0];
		// Always include my header file
		{
			char relativeIncludeBuffer[MAX_PATH_LENGTH];
			getFilenameFromPath(module->filename, relativeIncludeBuffer,
			                    sizeof(relativeIncludeBuffer));
			// TODO: hpp to h support
			strcat(relativeIncludeBuffer, ".hpp");
			addStringOutput(header.source, "#include", StringOutMod_SpaceAfter, blameToken);
			addStringOutput(header.source, relativeIncludeBuffer, StringOutMod_SurroundWithQuotes,
			                blameToken);
			addLangTokenOutput(header.source, StringOutMod_NewlineAfter, blameToken);
		}
		makeRunTimeHeaderFooter(header, footer, blameToken);
		outputSettings.heading = &header;
		outputSettings.footer = &footer;

		char sourceOutputName[MAX_PATH_LENGTH] = {0};
		if (!outputFilenameFromSourceFilename(manager.buildOutputDir.c_str(),
		                                      outputSettings.sourceCakelispFilename, "cpp",
		                                      sourceOutputName, sizeof(sourceOutputName)))
			return false;
		char headerOutputName[MAX_PATH_LENGTH] = {0};
		if (!outputFilenameFromSourceFilename(manager.buildOutputDir.c_str(),
		                                      outputSettings.sourceCakelispFilename, "hpp",
		                                      headerOutputName, sizeof(headerOutputName)))
			return false;
		module->sourceOutputName = sourceOutputName;
		module->headerOutputName = headerOutputName;
		outputSettings.sourceOutputName = module->sourceOutputName.c_str();
		outputSettings.headerOutputName = module->headerOutputName.c_str();

		if (!writeGeneratorOutput(*module->generatedOutput, nameSettings, formatSettings,
		                          outputSettings))
			return false;
	}

	if (logging.phases || logging.performance)
		Logf("Processed %d lines\n", g_totalLinesTokenized);

	return true;
}

static void OnCompileProcessOutput(const char* output)
{
	// TODO C/C++ error to Cakelisp token mapper
}

struct BuildObject
{
	int buildStatus;
	std::string sourceFilename;
	std::string filename;

	ProcessCommand* buildCommandOverride;
	std::vector<std::string> includesSearchDirs;
	std::vector<std::string> additionalOptions;

	// Only used for include scanning
	std::vector<std::string> headerSearchDirectories;
};

void buildObjectsFree(std::vector<BuildObject*>& objects)
{
	for (BuildObject* object : objects)
		delete object;

	objects.clear();
}

void copyModuleBuildOptionsToBuildObject(Module* module, ProcessCommand* buildCommandOverride,
                                         BuildObject* object)
{
	object->buildCommandOverride = buildCommandOverride;

	for (const std::string& searchDir : module->cSearchDirectories)
	{
		char searchDirToArgument[MAX_PATH_LENGTH + 2];
		makeIncludeArgument(searchDirToArgument, sizeof(searchDirToArgument), searchDir.c_str());
		object->includesSearchDirs.push_back(searchDirToArgument);
	}

	PushBackAll(object->headerSearchDirectories, module->cSearchDirectories);

	PushBackAll(object->additionalOptions, module->additionalBuildOptions);
}

// Copy cachedOutputExecutable to finalOutputNameOut, adding executable permissions
// TODO: There's no easy way to know whether this exe is the current build configuration's
// output exe, so copy it every time
bool copyExecutableToFinalOutput(const std::string& cachedOutputExecutable,
                                 const std::string& finalOutputName)
{
	if (logging.fileSystem)
		Log("Copying executable from cache\n");

	if (!copyBinaryFileTo(cachedOutputExecutable.c_str(), finalOutputName.c_str()))
	{
		Log("error: failed to copy executable from cache\n");
		return false;
	}

// TODO: Consider a better place for this
#ifdef WINDOWS
	char executableLib[MAX_PATH_LENGTH] = {0};
	SafeSnprintf(executableLib, sizeof(executableLib), "%s", cachedOutputExecutable.c_str());

	bool modifiedExtension = changeExtension(executableLib, "lib");

	if (modifiedExtension && fileExists(executableLib))
	{
		char finalOutputLib[MAX_PATH_LENGTH] = {0};
		SafeSnprintf(finalOutputLib, sizeof(finalOutputLib), "%s", finalOutputName.c_str());
		modifiedExtension = changeExtension(finalOutputLib, "lib");

		if (modifiedExtension && !copyBinaryFileTo(executableLib, finalOutputLib))
		{
			Log("error: failed to copy executable lib from cache\n");
			return false;
		}
	}
#endif

	addExecutablePermission(finalOutputName.c_str());
	return true;
}

static void addStringIfUnique(std::vector<std::string>& output, const char* stringToAdd)
{
	if (FindInContainer(output, stringToAdd) == output.end())
		output.push_back(stringToAdd);
}

struct SharedBuildOptions
{
	std::vector<std::string>* cSearchDirectories;
	ProcessCommand* buildCommand;
	ProcessCommand* linkCommand;
	std::string* executableOutput;
	// Cached directory, not necessarily the final artifacts directory (e.g. executable-output
	// option sets different location for the final executable)
	std::string* buildOutputDir;
	std::vector<CompileTimeHook>* preLinkHooks;

	// Link options
	std::vector<std::string> linkLibraries;
	std::vector<std::string> librarySearchDirs;
	std::vector<std::string> libraryRuntimeSearchDirs;
	std::vector<std::string> compilerLinkOptions;
	std::vector<std::string> toLinkerOptions;
};

static bool moduleManagerGetObjectsToBuild(ModuleManager& manager,
                                           std::vector<BuildObject*>& buildObjects,
                                           SharedBuildOptions& sharedBuildOptions)
{
	sharedBuildOptions.cSearchDirectories = &manager.environment.cSearchDirectories;
	sharedBuildOptions.buildCommand = &manager.environment.buildTimeBuildCommand;
	sharedBuildOptions.linkCommand = &manager.environment.buildTimeLinkCommand;
	sharedBuildOptions.executableOutput = &manager.environment.executableOutput;
	sharedBuildOptions.buildOutputDir = &manager.buildOutputDir;
	sharedBuildOptions.preLinkHooks = &manager.environment.preLinkHooks;

	int numModules = manager.modules.size();
	for (int moduleIndex = 0; moduleIndex < numModules; ++moduleIndex)
	{
		Module* module = manager.modules[moduleIndex];

		for (const CompileTimeHook& hook : module->preBuildHooks)
		{
			if (!((ModulePreBuildHook)hook.function)(manager, module))
			{
				Log("error: hook returned failure. Aborting build\n");
				buildObjectsFree(buildObjects);
				return false;
			}
		}

		ProcessCommand* buildCommandOverride = nullptr;
		{
			int buildCommandState = 0;
			if (!module->buildTimeBuildCommand.fileToExecute.empty())
				++buildCommandState;
			if (!module->buildTimeBuildCommand.arguments.empty())
				++buildCommandState;
			bool buildCommandValid = buildCommandState == 2;
			if (!buildCommandValid && buildCommandState)
			{
				ErrorAtTokenf(
				    (*module->tokens)[0],
				    "error: module build command override must be completely defined. Missing %s\n",
				    module->buildTimeBuildCommand.fileToExecute.empty() ? "file to execute" :
				                                                          "arguments");
				buildObjectsFree(buildObjects);
				return false;
			}

			if (buildCommandValid)
				buildCommandOverride = &module->buildTimeBuildCommand;
		}

		if (logging.buildProcess)
			Logf("Build module %s\n", module->sourceOutputName.c_str());

		std::vector<std::string> dependencyResolveDirectories;
		{
			dependencyResolveDirectories.reserve(manager.environment.cSearchDirectories.size() +
			                                     module->cSearchDirectories.size());
			PushBackAll(dependencyResolveDirectories, module->cSearchDirectories);
			PushBackAll(dependencyResolveDirectories, manager.environment.cSearchDirectories);
		}

		for (ModuleDependency& dependency : module->dependencies)
		{
			if (logging.buildProcess)
				Logf("\tRequires %s\n", dependency.name.c_str());

			// Cakelisp files are built at the module manager level, so we need not concern
			// ourselves with them
			if (dependency.type == ModuleDependency_Cakelisp)
				continue;

			if (dependency.type == ModuleDependency_CFile)
			{
				char resolvedDependencyFilename[MAX_PATH_LENGTH] = {0};
				if (!searchForFileInPathsWithError(
				        dependency.name.c_str(),
				        /*encounteredInFile=*/module->filename, dependencyResolveDirectories,
				        resolvedDependencyFilename, ArraySize(resolvedDependencyFilename),
				        *dependency.blameToken))
					return false;

				BuildObject* newBuildObject = new BuildObject;
				newBuildObject->buildStatus = 0;
				newBuildObject->sourceFilename = resolvedDependencyFilename;

				char buildObjectName[MAX_PATH_LENGTH] = {0};
				if (!outputFilenameFromSourceFilename(
				        manager.buildOutputDir.c_str(), newBuildObject->sourceFilename.c_str(),
				        compilerObjectExtension, buildObjectName, sizeof(buildObjectName)))
				{
					delete newBuildObject;
					Log("error: failed to create suitable output filename");
					buildObjectsFree(buildObjects);
					return false;
				}

				newBuildObject->filename = buildObjectName;

				// This is a bit weird to automatically use the parent module's build command
				copyModuleBuildOptionsToBuildObject(module, buildCommandOverride, newBuildObject);

				buildObjects.push_back(newBuildObject);
			}
		}

		// Add link arguments
		{
			struct
			{
				std::vector<std::string>* inputs;
				std::vector<std::string>* output;
			} linkArgumentsToAdd[]{
			    {&module->toLinkerOptions, &sharedBuildOptions.toLinkerOptions},
			    {&module->compilerLinkOptions, &sharedBuildOptions.compilerLinkOptions},
			    {&module->libraryDependencies, &sharedBuildOptions.linkLibraries},
			    {&module->librarySearchDirectories, &sharedBuildOptions.librarySearchDirs},
			    {&module->libraryRuntimeSearchDirectories,
			     &sharedBuildOptions.libraryRuntimeSearchDirs}};
			for (size_t linkArgumentSet = 0; linkArgumentSet < ArraySize(linkArgumentsToAdd);
			     ++linkArgumentSet)
			{
				for (const std::string& str : *(linkArgumentsToAdd[linkArgumentSet].inputs))
					addStringIfUnique(*(linkArgumentsToAdd[linkArgumentSet].output), str.c_str());
			}
		}

		if (module->skipBuild)
			continue;

		char buildObjectName[MAX_PATH_LENGTH] = {0};
		if (!outputFilenameFromSourceFilename(
		        manager.buildOutputDir.c_str(), module->sourceOutputName.c_str(),
		        compilerObjectExtension, buildObjectName, sizeof(buildObjectName)))
		{
			Log("error: failed to create suitable output filename");
			buildObjectsFree(buildObjects);
			return false;
		}

		// At this point, we do want to build the object. We might skip building it if it is cached.
		// In that case, the status code should still be 0, as if we built and succeeded building it
		BuildObject* newBuildObject = new BuildObject;
		newBuildObject->buildStatus = 0;
		newBuildObject->sourceFilename = module->sourceOutputName.c_str();
		newBuildObject->filename = buildObjectName;

		copyModuleBuildOptionsToBuildObject(module, buildCommandOverride, newBuildObject);

		buildObjects.push_back(newBuildObject);
	}

	return true;
}

// On successful build (true return value), you need to free buildObjects once you're done with them
bool moduleManagerBuild(ModuleManager& manager, std::vector<BuildObject*>& buildObjects,
                        SharedBuildOptions& buildOptions)
{
	int currentNumProcessesSpawned = 0;

	if (buildObjects.empty())
	{
		Log("Nothing to build. This may break the various hooks which expect something to be "
		    "built\n");
		buildObjectsFree(buildObjects);
		return false;
	}

	HeaderModificationTimeTable headerModifiedCache;

	for (BuildObject* object : buildObjects)
	{
		std::vector<const char*> searchDirArgs;
		searchDirArgs.reserve(object->includesSearchDirs.size() +
		                      buildOptions.cSearchDirectories->size());
		for (const std::string& searchDirArg : object->includesSearchDirs)
		{
			searchDirArgs.push_back(searchDirArg.c_str());
		}

		// This code sucks
		std::vector<std::string> globalSearchDirArgs;
		globalSearchDirArgs.reserve(buildOptions.cSearchDirectories->size());
		for (const std::string& searchDir : *buildOptions.cSearchDirectories)
		{
			char searchDirToArgument[MAX_PATH_LENGTH + 2];
			makeIncludeArgument(searchDirToArgument, sizeof(searchDirToArgument),
			                    searchDir.c_str());
			globalSearchDirArgs.push_back(searchDirToArgument);
			searchDirArgs.push_back(globalSearchDirArgs.back().c_str());
		}

		std::vector<const char*> additionalOptions;
		for (const std::string& option : object->additionalOptions)
		{
			additionalOptions.push_back(option.c_str());
		}

		ProcessCommand& buildCommand = object->buildCommandOverride ?
		                                   *object->buildCommandOverride :
		                                   *buildOptions.buildCommand;

		// Annoying exception for MSVC not having spaces between some arguments
		std::string* objectOutput = &object->filename;
		std::string objectOutputOverride;
		if (StrCompareIgnoreCase(buildCommand.fileToExecute.c_str(), "CL.exe") == 0)
		{
			char msvcObjectOutput[MAX_PATH_LENGTH] = {0};
			makeObjectOutputArgument(msvcObjectOutput, sizeof(msvcObjectOutput),
			                         object->filename.c_str());
			objectOutputOverride = msvcObjectOutput;
			objectOutput = &objectOutputOverride;
		}

		char debugSymbolsName[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(debugSymbolsName, "%s.%s", object->filename.c_str(),
		             compilerDebugSymbolsExtension);
		char debugSymbolsArgument[MAX_PATH_LENGTH] = {0};
		makeDebugSymbolsOutputArgument(debugSymbolsArgument, sizeof(debugSymbolsArgument),
		                               debugSymbolsName);

		char buildTimeBuildExecutable[MAX_PATH_LENGTH] = {0};
		if (!resolveExecutablePath(buildCommand.fileToExecute.c_str(), buildTimeBuildExecutable,
		                           sizeof(buildTimeBuildExecutable)))
		{
			buildObjectsFree(buildObjects);
			return false;
		}

		ProcessCommandInput buildTimeInputs[] = {
		    {ProcessCommandArgumentType_SourceInput, {object->sourceFilename.c_str()}},
		    {ProcessCommandArgumentType_ObjectOutput, {objectOutput->c_str()}},
		    {ProcessCommandArgumentType_DebugSymbolsOutput, {debugSymbolsArgument}},
		    {ProcessCommandArgumentType_IncludeSearchDirs, std::move(searchDirArgs)},
		    {ProcessCommandArgumentType_AdditionalOptions, std::move(additionalOptions)}};
		const char** buildArguments =
		    MakeProcessArgumentsFromCommand(buildTimeBuildExecutable, buildCommand.arguments,
		                                    buildTimeInputs, ArraySize(buildTimeInputs));
		if (!buildArguments)
		{
			Log("error: failed to construct build arguments\n");
			buildObjectsFree(buildObjects);
			return false;
		}

		// Can we use the cached version?
		{
			std::vector<std::string> headerSearchDirectories;
			{
				headerSearchDirectories.reserve(object->headerSearchDirectories.size() +
				                                buildOptions.cSearchDirectories->size() + 1);
				// Must include CWD to find generated cakelisp files
				headerSearchDirectories.push_back(".");
				PushBackAll(headerSearchDirectories, object->headerSearchDirectories);
				PushBackAll(headerSearchDirectories, *buildOptions.cSearchDirectories);
			}

			if (!cppFileNeedsBuild(manager.environment, object->sourceFilename.c_str(),
			                       object->filename.c_str(), buildArguments,
			                       manager.cachedCommandCrcs, manager.newCommandCrcs,
			                       headerModifiedCache, headerSearchDirectories))
			{
				free(buildArguments);
				continue;
			}
		}

		// Annoying Windows workaround: delete PDB to fix fatal error C1052
		// Technically we only need to do this for /DEBUG:fastlink
		if (debugSymbolsArgument[0] && fileExists(debugSymbolsName))
			remove(debugSymbolsName);

		// Go through with the build
		RunProcessArguments compileArguments = {};
		compileArguments.fileToExecute = buildTimeBuildExecutable;
		compileArguments.arguments = buildArguments;
		// PrintProcessArguments(buildArguments);

		if (runProcess(compileArguments, &object->buildStatus) != 0)
		{
			Log("error: failed to invoke compiler\n");
			free(buildArguments);
			buildObjectsFree(buildObjects);
			return false;
		}

		free(buildArguments);

		// TODO This could be made smarter by allowing more spawning right when a process
		// closes, instead of starting in waves
		++currentNumProcessesSpawned;
		if (currentNumProcessesSpawned >= maxProcessesRecommendedSpawned)
		{
			waitForAllProcessesClosed(OnCompileProcessOutput);
			currentNumProcessesSpawned = 0;
		}
	}

	if (logging.includeScanning || logging.performance)
		Logf(FORMAT_SIZE_T " files tested for modification times\n", headerModifiedCache.size());

	waitForAllProcessesClosed(OnCompileProcessOutput);
	currentNumProcessesSpawned = 0;

	bool succeededBuild = true;
	for (BuildObject* object : buildObjects)
	{
		int buildResult = object->buildStatus;
		if (buildResult != 0 || !fileExists(object->filename.c_str()))
		{
			Logf("error: failed to make target %s\n", object->filename.c_str());
			// Forget that the command was changed because the artifact wasn't successfully built
			manager.newCommandCrcs.erase(object->filename.c_str());
			succeededBuild = false;
			continue;
		}
	}

	if (!succeededBuild)
	{
		buildObjectsFree(buildObjects);
		return false;
	}

	return true;
}

bool moduleManagerLink(ModuleManager& manager, std::vector<BuildObject*>& buildObjects,
                       SharedBuildOptions& buildOptions, std::vector<std::string>& builtOutputs)
{
	std::string outputExecutableName;
	if (!buildOptions.executableOutput->empty())
	{
		char outputExecutableFilename[MAX_PATH_LENGTH] = {0};
		getFilenameFromPath(buildOptions.executableOutput->c_str(), outputExecutableFilename,
		                    sizeof(outputExecutableFilename));

		outputExecutableName = outputExecutableFilename;
	}
	if (outputExecutableName.empty())
		outputExecutableName = defaultExecutableName;

	char outputExecutableCachePath[MAX_PATH_LENGTH] = {0};
	if (!outputFilenameFromSourceFilename(
	        buildOptions.buildOutputDir->c_str(), outputExecutableName.c_str(),
	        /*addExtension=*/nullptr, outputExecutableCachePath, sizeof(outputExecutableCachePath)))
	{
		buildObjectsFree(buildObjects);
		return false;
	}
	outputExecutableName = outputExecutableCachePath;

	int numObjectsToLink = 0;
	bool objectsDirty = false;
	for (BuildObject* object : buildObjects)
	{
		if (logging.buildProcess)
			Logf("Need to link %s\n", object->filename.c_str());

		++numObjectsToLink;

		// If all our objects are older than our executable, don't even link!
		objectsDirty |= !canUseCachedFile(manager.environment, object->filename.c_str(),
		                                  outputExecutableName.c_str());
	}

	std::string finalOutputName;
	if (!buildOptions.executableOutput->empty())
		finalOutputName = *buildOptions.executableOutput;
	else
		finalOutputName = defaultExecutableName;

	bool succeededBuild = false;
	if (numObjectsToLink)
	{
		std::vector<const char*> objectsToLink(numObjectsToLink);
		for (int i = 0; i < numObjectsToLink; ++i)
		{
			BuildObject* object = buildObjects[i];

			objectsToLink[i] = object->filename.c_str();
		}

		// Copy it so hooks can modify it
		ProcessCommand linkCommand = *buildOptions.linkCommand;

		std::vector<std::string> executableOutputString;
		executableOutputString.push_back(outputExecutableName);

		// Various arguments need prefixes added. Do that here
		std::vector<const char*> executableToArgs;
		std::vector<const char*> librariesArgs;
		std::vector<const char*> librarySearchDirsArgs;
		std::vector<const char*> libraryRuntimeSearchDirsArgs;
		std::vector<const char*> convertedLinkerArgs;
		std::vector<const char*> compilerLinkArgs;
		BuildArgumentConverter convertedArguments[] = {
		    {&executableOutputString, {}, &executableToArgs, makeExecutableOutputArgument},
		    {&buildOptions.linkLibraries, {}, &librariesArgs, makeLinkLibraryArgument},
		    {&buildOptions.librarySearchDirs,
		     {},
		     &librarySearchDirsArgs,
		     makeLinkLibrarySearchDirArgument},
		    {&buildOptions.libraryRuntimeSearchDirs,
		     {},
		     &libraryRuntimeSearchDirsArgs,
		     makeLinkLibraryRuntimeSearchDirArgument},
		    {&buildOptions.toLinkerOptions, {}, &convertedLinkerArgs, makeLinkerArgument},
		    // We can't know how to auto-convert these because they could be anything
		    {&buildOptions.compilerLinkOptions, {}, &compilerLinkArgs, nullptr}};
		convertBuildArguments(convertedArguments, ArraySize(convertedArguments),
		                      linkCommand.fileToExecute.c_str());

		ProcessCommandInput linkTimeInputs[] = {
		    {ProcessCommandArgumentType_ExecutableOutput, executableToArgs},
		    {ProcessCommandArgumentType_ObjectInput, objectsToLink},
		    {ProcessCommandArgumentType_AdditionalOptions, compilerLinkArgs},
		    {ProcessCommandArgumentType_LibrarySearchDirs, librarySearchDirsArgs},
		    {ProcessCommandArgumentType_Libraries, librariesArgs},
		    {ProcessCommandArgumentType_LibraryRuntimeSearchDirs, libraryRuntimeSearchDirsArgs},
		    {ProcessCommandArgumentType_LinkerArguments, convertedLinkerArgs}};

		// Hooks should cooperate with eachother, i.e. try to only add things
		for (const CompileTimeHook& preLinkHook : *buildOptions.preLinkHooks)
		{
			if (!((PreLinkHook)preLinkHook.function)(manager, linkCommand, linkTimeInputs,
			                                         ArraySize(linkTimeInputs)))
			{
				Log("error: hook returned failure. Aborting build\n");
				buildObjectsFree(buildObjects);
				return false;
			}
		}

		char buildTimeLinkExecutable[MAX_PATH_LENGTH] = {0};
		if (!resolveExecutablePath(linkCommand.fileToExecute.c_str(), buildTimeLinkExecutable,
		                           sizeof(buildTimeLinkExecutable)))
		{
			buildObjectsFree(buildObjects);
			return false;
		}

		const char** linkArgumentList =
		    MakeProcessArgumentsFromCommand(buildTimeLinkExecutable, linkCommand.arguments,
		                                    linkTimeInputs, ArraySize(linkTimeInputs));
		if (!linkArgumentList)
		{
			buildObjectsFree(buildObjects);
			return false;
		}

		uint32_t commandCrc = 0;
		bool commandEqualsCached = commandEqualsCachedCommand(
		    manager.cachedCommandCrcs, finalOutputName.c_str(), linkArgumentList, &commandCrc);

		// Check if we can use the cached version
		if (!objectsDirty && commandEqualsCached)
		{
			if (logging.buildProcess)
				Log("Skipping linking (no built objects are newer than cached executable, command "
				    "identical)\n");

			{
				if (!copyExecutableToFinalOutput(outputExecutableName, finalOutputName))
				{
					free(linkArgumentList);
					buildObjectsFree(buildObjects);
					return false;
				}

				Logf("No changes needed for %s\n", finalOutputName.c_str());
				builtOutputs.push_back(finalOutputName);
			}

			free(linkArgumentList);
			buildObjectsFree(buildObjects);
			return true;
		}

		if (logging.buildReasons)
		{
			Logf("Link %s reason(s):\n", finalOutputName.c_str());
			if (objectsDirty)
				Log("\tobject files updated\n");
			if (!commandEqualsCached)
				Log("\tcommand changed since last run\n");
		}

		if (!commandEqualsCached)
			manager.newCommandCrcs[finalOutputName] = commandCrc;

		RunProcessArguments linkArguments = {};
		linkArguments.fileToExecute = buildTimeLinkExecutable;
		linkArguments.arguments = linkArgumentList;
		int linkStatus = 0;
		if (runProcess(linkArguments, &linkStatus) != 0)
		{
			free(linkArgumentList);
			buildObjectsFree(buildObjects);
			return false;
		}

		free(linkArgumentList);

		waitForAllProcessesClosed(OnCompileProcessOutput);

		succeededBuild = linkStatus == 0;
	}

	if (!succeededBuild)
	{
		// Forget that the command was changed because the artifact wasn't successfully built
		manager.newCommandCrcs.erase(finalOutputName);
		buildObjectsFree(buildObjects);
		return false;
	}

	{
		if (!copyExecutableToFinalOutput(outputExecutableName, finalOutputName))
		{
			buildObjectsFree(buildObjects);
			return false;
		}

		Logf("Successfully built and linked %s\n", finalOutputName.c_str());
		builtOutputs.push_back(finalOutputName);
	}

	buildObjectsFree(buildObjects);
	return true;
}

bool moduleManagerBuildAndLink(ModuleManager& manager, std::vector<std::string>& builtOutputs)
{
	if (!buildReadCacheFile(manager.buildOutputDir.c_str(), manager.cachedCommandCrcs))
		return false;

	// Pointer because the objects can't move, status codes are pointed to
	std::vector<BuildObject*> buildObjects;
	SharedBuildOptions buildOptions = {0};
	if (!moduleManagerGetObjectsToBuild(manager, buildObjects, buildOptions))
		return false;

	if (!moduleManagerBuild(manager, buildObjects, buildOptions))
	{
		// Remember any succeeded artifact command CRCs so they don't get forgotten just because
		// some others failed
		buildWriteCacheFile(manager.buildOutputDir.c_str(), manager.cachedCommandCrcs,
		                    manager.newCommandCrcs);
		return false;
	}

	if (!moduleManagerLink(manager, buildObjects, buildOptions, builtOutputs))
	{
		// Remember any succeeded artifact command CRCs so they don't get forgotten just because
		// some others failed
		buildWriteCacheFile(manager.buildOutputDir.c_str(), manager.cachedCommandCrcs,
		                    manager.newCommandCrcs);
		return false;
	}

	buildWriteCacheFile(manager.buildOutputDir.c_str(), manager.cachedCommandCrcs,
	                    manager.newCommandCrcs);

	return true;
}

void OnExecuteProcessOutput(const char* output)
{
}

bool moduleManagerExecuteBuiltOutputs(ModuleManager& manager,
                                      const std::vector<std::string>& builtOutputs)
{
	if (logging.phases)
		Log("\nExecute:\n");

	if (builtOutputs.empty())
	{
		Log("error: trying to execute, butn o executables were output\n");
		return false;
	}

	// TODO: Allow user to forward arguments to executable
	for (const std::string& output : builtOutputs)
	{
		RunProcessArguments arguments = {};
		// Need to use absolute path when executing
		const char* executablePath = makeAbsolutePath_Allocated(nullptr, output.c_str());
		arguments.fileToExecute = executablePath;
		const char* commandLineArguments[] = {StrDuplicate(arguments.fileToExecute), nullptr};
		arguments.arguments = commandLineArguments;
		char workingDirectory[MAX_PATH_LENGTH] = {0};
		getDirectoryFromPath(arguments.fileToExecute, workingDirectory,
		                     ArraySize(workingDirectory));
		arguments.workingDirectory = workingDirectory;
		int status = 0;

		if (runProcess(arguments, &status) != 0)
		{
			Logf("error: execution of %s failed\n", output.c_str());
			free((void*)executablePath);
			free((void*)commandLineArguments[0]);
			return false;
		}

		waitForAllProcessesClosed(OnExecuteProcessOutput);

		free((void*)executablePath);
		free((void*)commandLineArguments[0]);

		if (status != 0)
		{
			Logf("error: execution of %s returned non-zero exit code %d\n", output.c_str(), status);
			// Why not return the exit code? Because some exit codes end up becoming 0 after the
			// mod 256. I'm not really sure how other programs handle this
			return false;
		}
	}

	return true;
}
