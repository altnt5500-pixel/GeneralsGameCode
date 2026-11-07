#include <windows.h>
#include <cstdio>
#include <cstring>

#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/SubsystemInterface.h"
#include "Common/INI.h"
#include "Common/Science.h"
#include "Common/ThingFactory.h"
#include "Common/GameAudio.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/DamageFX.h"
#include "Common/Upgrade.h"
#include "Common/ModuleFactory.h"
#include "Common/PlayerTemplate.h"
#include "Common/MultiplayerSettings.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Armor.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameClient/Anim2D.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/FXList.h"
#include "GameClient/VideoPlayer.h"
#include "GameLogic/Locomotor.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "MilesAudioDevice/MilesAudioManager.h"

static SubsystemInterfaceList g_subsystemList;
static UnsignedInt g_errorCount = 0;
static UnsignedInt g_warningCount = 0;
static HANDLE g_consoleOutput = INVALID_HANDLE_VALUE;
static Bool g_useVirtualTerminal = FALSE;
static Bool g_mapValidation = FALSE;

enum ConsoleColor
{
	CONSOLE_COLOR_ORANGE,
	CONSOLE_COLOR_YELLOW,
	CONSOLE_COLOR_AQUA,
	CONSOLE_COLOR_PURPLE,
	CONSOLE_COLOR_RED,
	CONSOLE_COLOR_GREEN,
	CONSOLE_COLOR_WHITE
};

HINSTANCE ApplicationHInstance = nullptr;
HWND ApplicationHWnd = nullptr;
const char *gAppPrefix = "INI_";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return 0;
}

static void initializeConsoleColors()
{
	g_consoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD consoleMode = 0;
	g_useVirtualTerminal = GetConsoleMode(g_consoleOutput, &consoleMode) &&
		SetConsoleMode(g_consoleOutput, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static void setConsoleColor(ConsoleColor color)
{
	if (g_useVirtualTerminal)
	{
		int red = 255;
		int green = 255;
		int blue = 255;
		switch (color)
		{
		case CONSOLE_COLOR_ORANGE: red = 255; green = 165; blue = 0; break;
		case CONSOLE_COLOR_YELLOW: red = 255; green = 255; blue = 0; break;
		case CONSOLE_COLOR_AQUA: red = 0; green = 255; blue = 255; break;
		case CONSOLE_COLOR_PURPLE: red = 180; green = 0; blue = 255; break;
		case CONSOLE_COLOR_RED: red = 255; green = 0; blue = 0; break;
		case CONSOLE_COLOR_GREEN: red = 0; green = 255; blue = 0; break;
		case CONSOLE_COLOR_WHITE: break;
		}
		std::printf("\x1b[38;2;%d;%d;%dm", red, green, blue);
		return;
	}

	WORD attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
	switch (color)
	{
	case CONSOLE_COLOR_ORANGE: attributes = FOREGROUND_RED | FOREGROUND_GREEN; break;
	case CONSOLE_COLOR_YELLOW: attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
	case CONSOLE_COLOR_AQUA: attributes = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
	case CONSOLE_COLOR_PURPLE: attributes = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
	case CONSOLE_COLOR_RED: attributes = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
	case CONSOLE_COLOR_GREEN: attributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
	case CONSOLE_COLOR_WHITE: break;
	}
	SetConsoleTextAttribute(g_consoleOutput, attributes);
}

static void printBanner(const char *mode = nullptr)
{
	setConsoleColor(CONSOLE_COLOR_ORANGE);
	std::printf("======== INI Validator ========\n");
	setConsoleColor(CONSOLE_COLOR_YELLOW);
	std::printf("Version: ");
	setConsoleColor(CONSOLE_COLOR_AQUA);
	std::printf("2");
	setConsoleColor(CONSOLE_COLOR_YELLOW);
	std::printf(".");
	setConsoleColor(CONSOLE_COLOR_AQUA);
	std::printf("0\n");
	setConsoleColor(CONSOLE_COLOR_YELLOW);
	std::printf("Created by: ");
	setConsoleColor(CONSOLE_COLOR_ORANGE);
	std::printf("CookieLandProjects");
	setConsoleColor(CONSOLE_COLOR_YELLOW);
	std::printf(" - ");
	setConsoleColor(CONSOLE_COLOR_PURPLE);
	std::printf("n0ttws\n");
	if (mode != nullptr)
	{
		setConsoleColor(CONSOLE_COLOR_YELLOW);
		std::printf("Mode: ");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		std::printf("%s\n", mode);
	}
	setConsoleColor(CONSOLE_COLOR_WHITE);
}

static void reportINIWarning(const char *filename, UnsignedInt lineNum, const char *message)
{
	if (g_mapValidation && std::strstr(filename, "Map.ini") != nullptr &&
		(std::strstr(message, "Duplicate Object definition") != nullptr ||
			(std::strstr(message, "ModuleTag '") != nullptr && std::strstr(message, "repeated") != nullptr)))
	{
		return;
	}

	++g_warningCount;
	setConsoleColor(CONSOLE_COLOR_YELLOW);
	std::printf("[WARNING] %s:%u: %s\n\n", filename, lineNum, message);
	setConsoleColor(CONSOLE_COLOR_WHITE);
	std::fflush(stdout);
}

static void clearConsole()
{
	if (g_useVirtualTerminal)
	{
		std::printf("\x1b[3J\x1b[2J\x1b[H");
		std::fflush(stdout);
		return;
	}

	CONSOLE_SCREEN_BUFFER_INFO screenInfo;
	if (GetConsoleScreenBufferInfo(g_consoleOutput, &screenInfo))
	{
		DWORD cellCount = screenInfo.dwSize.X * screenInfo.dwSize.Y;
		DWORD cellsWritten = 0;
		COORD home = { 0, 0 };
		FillConsoleOutputCharacterA(g_consoleOutput, ' ', cellCount, home, &cellsWritten);
		FillConsoleOutputAttribute(g_consoleOutput, screenInfo.wAttributes, cellCount, home, &cellsWritten);
		SetConsoleCursorPosition(g_consoleOutput, home);
	}
}

static void printValidationSummary()
{
	setConsoleColor(CONSOLE_COLOR_WHITE);
	std::printf("Validation complete, ");
	setConsoleColor(CONSOLE_COLOR_RED);
	std::printf("%u error(s)", g_errorCount);
	setConsoleColor(CONSOLE_COLOR_WHITE);
	std::printf(" and ");
	setConsoleColor(CONSOLE_COLOR_YELLOW);
	std::printf("%u warning(s)", g_warningCount);
	setConsoleColor(CONSOLE_COLOR_WHITE);
	std::printf(" found.\n");
	std::fflush(stdout);
}

template <class SUBSYSTEM>
static void initTypedSubsystem(SUBSYSTEM *&subsystem, SUBSYSTEM *instance,
	const char *path1 = nullptr, const char *path2 = nullptr)
{
	subsystem = instance;
	g_subsystemList.initSubsystem(instance, path1, path2, nullptr);
}

static void reportINIError(const char *filename, UnsignedInt lineNum, const char *message)
{
	++g_errorCount;
	setConsoleColor(CONSOLE_COLOR_RED);
	std::printf("[FATAL_ERROR] %s:%u: %s\n\n", filename, lineNum, message);
	setConsoleColor(CONSOLE_COLOR_WHITE);
	std::fflush(stdout);
}

static void loadUnvisitedINIFiles(INI &ini)
{
	FilenameList filenames;
	TheFileSystem->getFileListInDirectory("Data\\", "*.ini", filenames, TRUE);
	if (filenames.empty())
	{
		std::printf("No .ini files found under Data.\n");
		return;
	}

	for (FilenameList::const_iterator it = filenames.begin(); it != filenames.end(); ++it)
	{
		if (!INI::wasFileLoadedForValidation(*it))
		{
			ini.load(*it, INI_LOAD_MULTIFILE, nullptr);
		}
	}
}

static int finish(int result)
{
	std::printf("Press Enter to close.\n");
	std::fflush(stdout);
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	DWORD consoleMode = 0;
	if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &consoleMode))
	{
		char line[16];
		DWORD charactersRead = 0;
		ReadConsoleA(input, line, sizeof(line), &charactersRead, nullptr);
	}
	else
	{
		std::getchar();
	}
	return result;
}

enum ValidationMode
{
	VALIDATION_MODE_MOD,
	VALIDATION_MODE_MAP
};

static Bool readConsoleLine(char *buffer, size_t bufferSize)
{
	if (std::fgets(buffer, (int)bufferSize, stdin) == nullptr)
	{
		return FALSE;
	}

	size_t lineLength = std::strcspn(buffer, "\r\n");
	if (buffer[lineLength] == '\0')
	{
		int character;
		while ((character = std::getchar()) != '\n' && character != EOF) {}
		return FALSE;
	}
	buffer[lineLength] = '\0';
	return TRUE;
}

static Bool getExecutableDirectory(char *directory, size_t directorySize)
{
	DWORD pathLength = GetModuleFileNameA(nullptr, directory, (DWORD)directorySize);
	if (pathLength == 0 || pathLength >= directorySize)
	{
		return FALSE;
	}

	char *lastSeparator = std::strrchr(directory, '\\');
	if (lastSeparator == nullptr)
	{
		return FALSE;
	}
	*lastSeparator = '\0';
	return TRUE;
}

static Bool chooseValidationMode(ValidationMode& mode)
{
	char input[32];
	for (;;)
	{
		setConsoleColor(CONSOLE_COLOR_WHITE);
		std::printf("Choose Validation type:\n");
		setConsoleColor(CONSOLE_COLOR_YELLOW);
		std::printf("[1]");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		std::printf(" Mod .ini Validation\n");
		setConsoleColor(CONSOLE_COLOR_YELLOW);
		std::printf("[2]");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		std::printf(" Map.ini Validation\n");
		std::printf("Enter 1 or 2: ");
		std::fflush(stdout);

		if (!readConsoleLine(input, sizeof(input)))
		{
			return FALSE;
		}
		if (std::strcmp(input, "1") == 0)
		{
			mode = VALIDATION_MODE_MOD;
			clearConsole();
			return TRUE;
		}
		if (std::strcmp(input, "2") == 0)
		{
			mode = VALIDATION_MODE_MAP;
			clearConsole();
			return TRUE;
		}

		setConsoleColor(CONSOLE_COLOR_RED);
		std::printf("Enter 1 or 2.\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
	}
}

static Bool promptForValidationPaths(ValidationMode mode, const char *executableDirectory,
	char *gameDirectory, char *mapDirectory)
{
	printBanner(mode == VALIDATION_MODE_MOD ? "Mod .ini Validation" : "Map.ini Validation");

	if (mode == VALIDATION_MODE_MOD)
	{
		setConsoleColor(CONSOLE_COLOR_YELLOW);
		std::printf("Enter your Zero Hour directory (or press enter if this file is located in that directory already):\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		if (!readConsoleLine(gameDirectory, MAX_PATH))
		{
			return FALSE;
		}
		if (gameDirectory[0] == '\0')
		{
			std::strcpy(gameDirectory, executableDirectory);
		}
	}
	else
	{
		std::strcpy(gameDirectory, executableDirectory);
		setConsoleColor(CONSOLE_COLOR_YELLOW);
		std::printf("Enter the directory to the exact map folder containing the Map.ini file:\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		if (!readConsoleLine(mapDirectory, MAX_PATH) || mapDirectory[0] == '\0')
		{
			return FALSE;
		}
	}

	return TRUE;
}

int main()
{
	initializeConsoleColors();
	char executableDirectory[MAX_PATH] = {};
	if (!getExecutableDirectory(executableDirectory, sizeof(executableDirectory)))
	{
		setConsoleColor(CONSOLE_COLOR_RED);
		std::printf("[ERROR] Could not locate the validator executable directory.\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		return finish(2);
	}
	SetCurrentDirectoryA(executableDirectory);

	printBanner();
	ValidationMode mode;
	if (!chooseValidationMode(mode))
	{
		return finish(2);
	}
	g_mapValidation = mode == VALIDATION_MODE_MAP;

	char gameDirectory[MAX_PATH] = {};
	char mapDirectory[MAX_PATH] = {};
	if (!promptForValidationPaths(mode, executableDirectory, gameDirectory, mapDirectory))
	{
		setConsoleColor(CONSOLE_COLOR_RED);
		std::printf("[ERROR] A required directory was not provided.\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		return finish(2);
	}

	if (!SetCurrentDirectoryA(gameDirectory))
	{
		setConsoleColor(CONSOLE_COLOR_RED);
		std::printf("[ERROR] Could not open the specified Zero Hour directory.\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		return finish(2);
	}
	const DWORD dataAttributes = GetFileAttributesA("Data");
	if (dataAttributes == INVALID_FILE_ATTRIBUTES || (dataAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		setConsoleColor(CONSOLE_COLOR_RED);
		std::printf("[ERROR] The specified directory must contain a Data folder.\n");
		setConsoleColor(CONSOLE_COLOR_WHITE);
		return finish(2);
	}
	char mapIniPath[MAX_PATH] = {};
	if (mode == VALIDATION_MODE_MAP)
	{
		char absoluteMapDirectory[MAX_PATH] = {};
		DWORD pathLength = GetFullPathNameA(mapDirectory, MAX_PATH, absoluteMapDirectory, nullptr);
		if (pathLength == 0 || pathLength >= MAX_PATH)
		{
			setConsoleColor(CONSOLE_COLOR_RED);
			std::printf("[ERROR] Could not resolve the map directory.\n");
			setConsoleColor(CONSOLE_COLOR_WHITE);
			return finish(2);
		}
		std::snprintf(mapIniPath, sizeof(mapIniPath), "%s\\Map.ini", absoluteMapDirectory);
		const DWORD mapAttributes = GetFileAttributesA(mapIniPath);
		if (mapAttributes == INVALID_FILE_ATTRIBUTES || (mapAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			setConsoleColor(CONSOLE_COLOR_RED);
			std::printf("[ERROR] The selected map folder does not contain Map.ini.\n");
			setConsoleColor(CONSOLE_COLOR_WHITE);
			return finish(2);
		}
	}

	setConsoleColor(CONSOLE_COLOR_WHITE);
	std::printf(mode == VALIDATION_MODE_MOD ? "Loading .ini files...\n\n" : "Loading Map.ini file...\n\n");
	std::fflush(stdout);

	initMemoryManager();
	try
	{
		ApplicationHInstance = GetModuleHandle(nullptr);
		TheNameKeyGenerator = new NameKeyGenerator;
		TheNameKeyGenerator->init();
		TheFileSystem = new FileSystem;

		initTypedSubsystem(TheLocalFileSystem, (LocalFileSystem *)new Win32LocalFileSystem);
		initTypedSubsystem(TheArchiveFileSystem, (ArchiveFileSystem *)new Win32BIGFileSystem);

		INI ini;
		INI::setValidationErrorCallback(reportINIError);
		INI::setValidationWarningCallback(reportINIWarning);
		initTypedSubsystem(TheWritableGlobalData, new GlobalData(), "Data\\INI\\Default\\GameData", "Data\\INI\\GameData");
		initTypedSubsystem(TheGameText, CreateGameTextInterface());
		initTypedSubsystem(TheScienceStore, new ScienceStore(), "Data\\INI\\Default\\Science", "Data\\INI\\Science");
		initTypedSubsystem(TheMultiplayerSettings, new MultiplayerSettings(), "Data\\INI\\Default\\Multiplayer", "Data\\INI\\Multiplayer");
		initTypedSubsystem(TheTerrainTypes, new TerrainTypeCollection(), "Data\\INI\\Default\\Terrain", "Data\\INI\\Terrain");
		initTypedSubsystem(TheTerrainRoads, new TerrainRoadCollection(), "Data\\INI\\Default\\Roads", "Data\\INI\\Roads");
		initTypedSubsystem(TheScriptEngine, new ScriptEngine());
		initTypedSubsystem(TheAudio, (AudioManager *)new MilesAudioManager());
		initTypedSubsystem(TheVideoPlayer, (VideoPlayerInterface *)new VideoPlayer());
		initTypedSubsystem(TheModuleFactory, (ModuleFactory *)new W3DModuleFactory());
		initTypedSubsystem(TheSidesList, new SidesList());
		initTypedSubsystem(TheCaveSystem, new CaveSystem());
		initTypedSubsystem(TheRankInfoStore, new RankInfoStore(), nullptr, "Data\\INI\\Rank");
		initTypedSubsystem(ThePlayerTemplateStore, new PlayerTemplateStore(), "Data\\INI\\Default\\PlayerTemplate", "Data\\INI\\PlayerTemplate");
		initTypedSubsystem(TheSpecialPowerStore, new SpecialPowerStore(), "Data\\INI\\Default\\SpecialPower", "Data\\INI\\SpecialPower");
		initTypedSubsystem(TheParticleSystemManager, (ParticleSystemManager *)new W3DParticleSystemManager());
		initTypedSubsystem(TheFXListStore, new FXListStore(), "Data\\INI\\Default\\FXList", "Data\\INI\\FXList");
		initTypedSubsystem(TheWeaponStore, new WeaponStore(), nullptr, "Data\\INI\\Weapon");
		initTypedSubsystem(TheObjectCreationListStore, new ObjectCreationListStore(), "Data\\INI\\Default\\ObjectCreationList", "Data\\INI\\ObjectCreationList");
		initTypedSubsystem(TheLocomotorStore, new LocomotorStore(), nullptr, "Data\\INI\\Locomotor");
		initTypedSubsystem(TheDamageFXStore, new DamageFXStore(), nullptr, "Data\\INI\\DamageFX");
		initTypedSubsystem(TheArmorStore, new ArmorStore(), nullptr, "Data\\INI\\Armor");
		initTypedSubsystem(TheThingFactory, new ThingFactory(), "Data\\INI\\Default\\Object", "Data\\INI\\Object");
		initTypedSubsystem(TheCrateSystem, new CrateSystem(), "Data\\INI\\Default\\Crate", "Data\\INI\\Crate");
		initTypedSubsystem(TheUpgradeCenter, new UpgradeCenter(), "Data\\INI\\Default\\Upgrade", "Data\\INI\\Upgrade");
		initTypedSubsystem(TheAnim2DCollection, new Anim2DCollection());

		if (g_errorCount == 0)
		{
			g_subsystemList.postProcessLoadAll();
			if (mode == VALIDATION_MODE_MOD)
			{
				loadUnvisitedINIFiles(ini);
			}
			else
			{
				ini.load(mapIniPath, INI_LOAD_MULTIFILE, nullptr);
			}
		}
		else
		{
			if (mode == VALIDATION_MODE_MAP)
			{
				reportINIWarning(mapIniPath, 0, "\n\nMap.ini was not validated because installation INIs failed.\n\n");
			}
			//std::printf("Skipping subsystem post-processing because INI errors were found.\n");
			//if (mode == VALIDATION_MODE_MOD)
			//{
			//	std::printf("Skipping remaining INIs because validation state is incomplete.\n");
			//}
		}

		printValidationSummary();
		const int result = g_errorCount == 0 ? 0 : 1;
		if (g_errorCount == 0)
		{
			setConsoleColor(CONSOLE_COLOR_GREEN);
			std::printf("Validation completed successfully. No errors found.\n");
			setConsoleColor(CONSOLE_COLOR_WHITE);
		}
		std::fflush(stdout);
		finish(result);
		g_subsystemList.shutdownAll();
		INI::clearValidationState();
		delete TheFileSystem;
		TheFileSystem = nullptr;
		delete TheNameKeyGenerator;
		TheNameKeyGenerator = nullptr;
	}
	catch (...)
	{
		reportINIError("INI Validator", 0, "Initialization failed before validation completed.");
		printValidationSummary();
		INI::clearValidationState();
		shutdownMemoryManager();
		return finish(1);
	}

	shutdownMemoryManager();
	return g_errorCount == 0 ? 0 : 1;
}
