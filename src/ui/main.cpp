#include "stdafx.h"

#include "SFML/Graphics.hpp"
#include "../core/Cpu.h"
#include "../core/MemoryMap.h"
#include "../core/Graphics.h"
#include "../core/CartridgeFactory.h"
#include "../core/SpriteManager.h"


MemoryMap EmuMemoryMap;
Cpu EmuCpu{ EmuMemoryMap };
SpriteManager EmuSpriteManager;
Graphics EmuGraphics{ EmuCpu, EmuMemoryMap, EmuSpriteManager };

void InitialiseEmulator()
{
	auto cartridge = CartridgeFactory::LoadFromFile("../../ROMs/gb-snake.gb", 0);
	EmuMemoryMap.SetCartridge(cartridge);
}

void RunCpu(int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget && EmuCpu.IsRunning())
	{
		currentCycle += EmuCpu.DoNextInstruction();
	}

	currentCycle = std::max(currentCycle, cycleTarget);
}

int* GetEmulatorFrame()
{
	EmuGraphics.ResetFrame();
	auto cycleCounter = 0;
	auto cycleTarget = 0;

	for (auto i = 0; i < Graphics::VertPixels; i++)
	{
		EmuGraphics.SetLcdcStatus(LcdcStatus::OamReadMode);
		cycleTarget += Graphics::OamReadClocks;
		RunCpu(cycleCounter, cycleTarget);

		EmuGraphics.SetLcdcStatus(LcdcStatus::OamAndVramReadMode);
		cycleTarget += Graphics::OamAndVramReadClocks;
		RunCpu(cycleCounter, cycleTarget);

		EmuGraphics.SetLcdcStatus(LcdcStatus::HBlankMode);
		cycleTarget += Graphics::HBlankPeriodClocks;
		RunCpu(cycleCounter, cycleTarget);

		EmuGraphics.RenderLine();
	}

	EmuGraphics.SetLcdcStatus(LcdcStatus::VBlankMode);

	for (auto i = 0; i < Graphics::VBlankLines; i++)
	{
		cycleTarget += Graphics::ScanlineClocks;
		RunCpu(cycleCounter, cycleTarget);

		EmuGraphics.RenderLine();
	}

	return EmuGraphics.Bitmap;
}

int main()
{
	InitialiseEmulator();

	sf::RenderWindow window(sf::VideoMode(640, 576), "EmuBoy");
	
	window.setFramerateLimit(60);

	sf::Texture texture;
	texture.create(Graphics::HozPixels, Graphics::HozPixels);
	texture.setSmooth(true);

	sf::Sprite sprite;
	sprite.setTexture(texture);
	sprite.scale(4, 4);

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed) window.close();
		}

		texture.update(reinterpret_cast<sf::Uint8*>(GetEmulatorFrame()));

		//window.clear();
		window.draw(sprite);
		window.display();
	}

	return 0;
}
