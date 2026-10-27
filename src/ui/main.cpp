#include "stdafx.h"
#include "../core/CartridgeFactory.h"
#include "../core/BatteryStore.h"
#include "App.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <csignal>
#include <exception>
#include <optional>
#include <vector>

// Set by Ctrl+C or the terminal closing, so the emulator can exit normally and save the game
volatile std::sig_atomic_t QuitRequested = 0;

void RequestQuit(int)
{
	QuitRequested = 1;
}

// Usage: EmuBoy [--dmg | --cgb] [rom] [record-file] [scale]
// With a record file, every frame is appended to it as raw 160x144 RGBA, as the emulator produced it. --dmg or
// --cgb runs the game on that Game Boy rather than the one its header asks for
int main(int argc, char* argv[])
{
	App::Options options;
	std::vector<std::string> arguments;
	for (auto i = 1; i < argc; i++)
	{
		std::string argument = argv[i];
		if (argument.compare(0, 2, "--") != 0)
		{
			arguments.push_back(argument);
			continue;
		}

		std::optional<GameBoyModel> model;
		if (argument == "--dmg") model = GameBoyModel::Dmg;
		else if (argument == "--cgb") model = GameBoyModel::Cgb;
		else
		{
			std::cerr << "Unknown option " << argument << ". Usage: EmuBoy [--dmg | --cgb] [rom] [record-file] [scale]" << std::endl;
			return 1;
		}

		if (options.Model && *options.Model != *model)
		{
			std::cerr << "Choose either --dmg or --cgb, not both" << std::endl;
			return 1;
		}

		options.Model = model;
	}

	options.RomPath = arguments.size() > 0 ? arguments[0] : "../../ROMs/gb-snake.gb";
	options.RecordPath = arguments.size() > 1 ? arguments[1] : "";
	options.Scale = arguments.size() > 2 ? std::max(1, std::atoi(arguments[2].c_str())) : 4;

	std::vector<unsigned char> rom;
	if (!CartridgeFactory::ReadFile(options.RomPath, rom))
	{
		std::cerr << "Couldn't read " << options.RomPath << std::endl;
		return 1;
	}

	if (!CartridgeFactory::TypeFromHeader(rom).IsSupported)
	{
		std::cerr << "Unsupported cartridge type $" << std::hex << static_cast<int>(rom[CartridgeFactory::CartridgeTypeHeaderOffset])
			<< "; emulating it as MBC1, so it may not run correctly" << std::endl;
	}

	auto title = CartridgeFactory::TitleFromHeader(rom);
	auto cartridge = CartridgeFactory::Create(std::move(rom));
	if (cartridge == nullptr) return 1;

	BatteryStore saves{ cartridge, BatteryStore::SavePathForRom(options.RomPath) };
	App::ReportSaveResult(saves.Load());

	std::signal(SIGINT, RequestQuit);
	std::signal(SIGTERM, RequestQuit);
#ifdef SIGHUP
	std::signal(SIGHUP, RequestQuit);
#endif

	// An unexpected error still saves the game before exiting
	try
	{
		App app{ options, cartridge, title, saves };
		app.Run(QuitRequested);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Emu Boy stopped after an error: " << e.what() << std::endl;
		App::ReportSaveResult(saves.SaveOnExit());
		return 1;
	}

	App::ReportSaveResult(saves.SaveOnExit());
	return 0;
}
