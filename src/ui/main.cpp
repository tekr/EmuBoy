#include "stdafx.h"
#include "SFML/Graphics.hpp"
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "../core/BatteryStore.h"
#include "../core/InputJoypad.h"
#include "AudioOutput.h"
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <csignal>

using sfKey = sf::Keyboard::Key;

std::vector<std::pair<sf::Keyboard::Key, JoypadKey>> KeyMap
{
	{ sfKey::D, JoypadKey::Right },
	{ sfKey::A, JoypadKey::Left },
	{ sfKey::W, JoypadKey::Up },
	{ sfKey::S, JoypadKey::Down },
	{ sfKey::Tab, JoypadKey::Select },
	{ sfKey::Return, JoypadKey::Start },
	{ sfKey::Comma, JoypadKey::B },
	{ sfKey::Period, JoypadKey::A }
};

// Joypad state comes from key events rather than sf::Keyboard::isKeyPressed, which on macOS
// needs the Input Monitoring permission
JoypadKey KeysDown = JoypadKey::NoKey;

void UpdateKeysDown(const sf::Event& event)
{
	for (auto keyPair : KeyMap)
	{
		if (event.key.code != keyPair.first) continue;

		KeysDown = static_cast<JoypadKey>(event.type == sf::Event::KeyPressed
			? KeysDown | keyPair.second
			: KeysDown & ~keyPair.second);
	}
}

// Set by Ctrl+C or the terminal closing, so the emulator can exit normally and save the game
volatile std::sig_atomic_t QuitRequested = 0;

void RequestQuit(int)
{
	QuitRequested = 1;
}

void ReportSaveResult(const BatteryStore::Result& result)
{
	if (!result.Message.empty()) std::cerr << result.Message << std::endl;
}

// Usage: EmuBoy [rom] [record-file] [scale]
// With a record file, every frame is appended to it as raw 160x144 RGBA, as the emulator produced it
int main(int argc, char* argv[])
{
	auto scale = argc > 3 ? std::max(1, std::atoi(argv[3])) : 4;
	sf::RenderWindow window(sf::VideoMode(Graphics::HozPixels * scale, Graphics::VertPixels * scale), "EmuBoy");

	sf::Texture frameTexture;
	frameTexture.create(Graphics::HozPixels, Graphics::VertPixels);
	sf::Sprite frameSprite(frameTexture);

	// Frames are drawn at 60% opacity over the previous ones to emulate the slow LCD. They build up in a texture
	// the app owns, as a window's back buffer isn't guaranteed to keep its contents between frames
	sf::RenderTexture screen;
	screen.create(Graphics::HozPixels, Graphics::VertPixels);
	screen.setSmooth(true);
	screen.clear();

	sf::Sprite screenSprite(screen.getTexture());
	screenSprite.scale(static_cast<float>(scale), static_cast<float>(scale));

	std::string romPath = argc > 1 ? argv[1] : "../../ROMs/gb-snake.gb";
	std::vector<unsigned char> rom;
	std::shared_ptr<Cartridge> cartridge;
	if (CartridgeFactory::ReadFile(romPath, rom))
	{
		if (!CartridgeFactory::TypeFromHeader(rom).IsSupported)
		{
			std::cerr << "Unsupported cartridge type $" << std::hex << static_cast<int>(rom[CartridgeFactory::CartridgeTypeHeaderOffset])
				<< "; emulating it as MBC1, so it may not run correctly" << std::endl;
		}

		cartridge = CartridgeFactory::Create(std::move(rom));
	}

	if (cartridge != nullptr)
	{
		BatteryStore saves{ cartridge, BatteryStore::SavePathForRom(romPath) };
		ReportSaveResult(saves.Load());

		std::signal(SIGINT, RequestQuit);
		std::signal(SIGTERM, RequestQuit);
#ifdef SIGHUP
		std::signal(SIGHUP, RequestQuit);
#endif

		// Changes are saved within a few seconds, so a crash loses little, without writing the file on every frame of
		// a game's multi-frame save routine
		const auto framesBetweenSaves = 3 * Graphics::SystemClockHz / Graphics::FrameClocks;
		auto framesUntilSave = framesBetweenSaves;

		Emulator emulator{ cartridge };
		AudioOutput audio{ emulator.GetAudioSampleRate() };
		std::vector<short> samples;
		std::ofstream record;
		if (argc > 2) record.open(argv[2], std::ios::binary);

		// Paced to the Game Boy's own frame rate (4194304 / 70224 = 59.73 Hz) against a running schedule, so
		// sleep overshoot doesn't accumulate the way it does with setFramerateLimit
		const auto frameTime = sf::microseconds(1000000LL * Graphics::FrameClocks / Graphics::SystemClockHz);
		sf::Clock clock;
		auto nextFrame = frameTime;

		while (window.isOpen())
		{
			if (QuitRequested)
			{
				window.close();
				break;
			}

			emulator.GetJoypad().SetKeysDown(window.hasFocus() ? KeysDown : JoypadKey::NoKey);

			sf::Event event;
			while (window.pollEvent(event))
			{
				if (event.type == sf::Event::Closed) window.close();
				if (event.type == sf::Event::KeyPressed || event.type == sf::Event::KeyReleased) UpdateKeysDown(event);
				if (event.type == sf::Event::LostFocus) KeysDown = JoypadKey::NoKey;
			}

			auto frame = emulator.GetFrame();
			frameTexture.update(reinterpret_cast<sf::Uint8*>(frame));
			screen.draw(frameSprite);
			screen.display();
			if (record) record.write(reinterpret_cast<char*>(frame), Graphics::HozPixels * Graphics::VertPixels * sizeof(*frame));

			emulator.TakeAudioSamples(samples);
			audio.Push(samples);
			if (audio.getStatus() != sf::SoundStream::Playing) audio.play();

			window.draw(screenSprite);
			window.display();

			if (--framesUntilSave == 0)
			{
				ReportSaveResult(saves.SaveIfChanged());
				framesUntilSave = framesBetweenSaves;
			}

			auto now = clock.getElapsedTime();
			if (now < nextFrame) sf::sleep(nextFrame - now);
			else if (now - nextFrame > frameTime * 4.f) nextFrame = now; // fell well behind (e.g. window dragged): don't race to catch up
			nextFrame += frameTime;
		}

		ReportSaveResult(saves.SaveOnExit());
		return 0;
	}

	return 1;
}
