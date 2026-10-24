#include "stdafx.h"
#include "SFML/Graphics.hpp"
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "../core/InputJoypad.h"
#include <fstream>
#include <string>
#include <cstdlib>
#include <algorithm>

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

// Usage: EmuBoy [rom] [record-file] [scale]
// With a record file, every frame is appended to it as raw 160x144 RGBA, as the emulator produced it
int main(int argc, char* argv[])
{
	auto scale = argc > 3 ? std::max(1, std::atoi(argv[3])) : 4;
	sf::RenderWindow window(sf::VideoMode(Graphics::HozPixels * scale, Graphics::VertPixels * scale), "EmuBoy");

	sf::Texture texture;
	texture.create(Graphics::HozPixels, Graphics::VertPixels);
	texture.setSmooth(true);

	sf::Sprite sprite;
	sprite.setTexture(texture);
	sprite.scale(static_cast<float>(scale), static_cast<float>(scale));

	auto cartridge = CartridgeFactory::LoadFromFile(argc > 1 ? argv[1] : "../../ROMs/gb-snake.gb", 0);
	if (cartridge != nullptr)
	{
		Emulator emulator{ cartridge };
		std::ofstream record;
		if (argc > 2) record.open(argv[2], std::ios::binary);

		// Paced to the Game Boy's own frame rate (4194304 / 70224 = 59.73 Hz) against a running schedule, so
		// sleep overshoot doesn't accumulate the way it does with setFramerateLimit
		const auto frameTime = sf::microseconds(1000000LL * Graphics::FrameClocks / Graphics::SystemClockHz);
		sf::Clock clock;
		auto nextFrame = frameTime;

		while (window.isOpen())
		{
			emulator.GetJoypad().SetKeysDown(window.hasFocus() ? KeysDown : JoypadKey::NoKey);

			sf::Event event;
			while (window.pollEvent(event))
			{
				if (event.type == sf::Event::Closed) window.close();
				if (event.type == sf::Event::KeyPressed || event.type == sf::Event::KeyReleased) UpdateKeysDown(event);
				if (event.type == sf::Event::LostFocus) KeysDown = JoypadKey::NoKey;
			}

			auto frame = emulator.GetFrame();
			texture.update(reinterpret_cast<sf::Uint8*>(frame));
			if (record) record.write(reinterpret_cast<char*>(frame), Graphics::HozPixels * Graphics::VertPixels * 4);

			window.draw(sprite);
			window.display();

			auto now = clock.getElapsedTime();
			if (now < nextFrame) sf::sleep(nextFrame - now);
			else if (now - nextFrame > frameTime * 4.f) nextFrame = now; // fell well behind (e.g. window dragged): don't race to catch up
			nextFrame += frameTime;
		}

		return 0;
	}

	return 1;
}
