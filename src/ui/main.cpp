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

// Set by Ctrl+C or the terminal closing, so the emulator can exit normally and save the game
volatile std::sig_atomic_t QuitRequested = 0;

void RequestQuit(int)
{
	QuitRequested = 1;
}

// Usage: EmuBoy [rom] [record-file] [scale]
// With a record file, every frame is appended to it as raw 160x144 RGBA, as the emulator produced it
int main(int argc, char* argv[])
{
	App::Options options;
	options.RomPath = argc > 1 ? argv[1] : "../../ROMs/gb-snake.gb";
	options.RecordPath = argc > 2 ? argv[2] : "";
	options.Scale = argc > 3 ? std::max(1, std::atoi(argv[3])) : 4;

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
