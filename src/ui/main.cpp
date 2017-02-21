#include "stdafx.h"
#include "SFML/Graphics.hpp"
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "../core/InputJoypad.h"

InputJoypad EmuJoypad;

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

JoypadKey GetKeysDown()
{
	auto keysDown = JoypadKey::NoKey;

	for (auto keyPair : KeyMap)
	{
		if (sf::Keyboard::isKeyPressed(keyPair.first)) keysDown = static_cast<JoypadKey>(keysDown | keyPair.second);
	}

	return keysDown;
}

int main()
{
	sf::RenderWindow window(sf::VideoMode(640, 576), "EmuBoy");
	window.setFramerateLimit(60);

	sf::Texture texture;
	texture.create(Graphics::HozPixels, Graphics::VertPixels);
	texture.setSmooth(true);

	sf::Sprite sprite;
	sprite.setTexture(texture);
	sprite.scale(4, 4);

	auto cartridge = CartridgeFactory::LoadFromFile("../../ROMs/gb-snake.gb", 0);
	if (cartridge != nullptr)
	{
		Emulator emulator{ cartridge };

		while (window.isOpen())
    {
      // Need to get EmuJoypad ref from emulator
		  EmuJoypad.SetKeysDown(window.hasFocus() ? GetKeysDown() : JoypadKey::NoKey);

			sf::Event event;
			while (window.pollEvent(event))
			{
				if (event.type == sf::Event::Closed) window.close();
			}

			texture.update(reinterpret_cast<sf::Uint8*>(emulator.GetFrame()));

			window.draw(sprite);
			window.display();
		}

		return 0;
	}

	return 1;
}
