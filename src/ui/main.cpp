#include "stdafx.h"
#include "SFML/Graphics.hpp"
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

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
