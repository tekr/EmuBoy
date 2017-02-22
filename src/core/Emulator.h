#pragma once
#include "Graphics.h"
#include <algorithm>
#include "MemoryMap.h"
#include "SpriteManager.h"
#include "Cpu.h"
#include "Timer.h"

class Emulator
{
	InputJoypad EmuJoypad;
	MemoryMap EmuMemoryMap { EmuJoypad };
	Cpu EmuCpu{ EmuMemoryMap };
	SpriteManager EmuSpriteManager;
	Graphics EmuGraphics{ EmuCpu, EmuMemoryMap, EmuSpriteManager };
	Timer EmuTimer;

	void Run(int& currentCycle, int cycleTarget);

public:
	explicit Emulator(std::shared_ptr<Cartridge> cartridge);
	~Emulator();

	int* GetFrame();
	InputJoypad& GetJoypad() { return EmuJoypad; }
};

