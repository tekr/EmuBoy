#pragma once
#include "Graphics.h"
#include "MemoryMap.h"
#include "SpriteManager.h"
#include "Cpu.h"
#include "Timer.h"
#include "Apu.h"
#include <vector>

class Emulator
{
	InputJoypad EmuJoypad;
	MemoryMap EmuMemoryMap { EmuJoypad };
	Cpu EmuCpu{ EmuMemoryMap };
	SpriteManager EmuSpriteManager;
	Graphics EmuGraphics{ EmuCpu, EmuMemoryMap, EmuSpriteManager };
	Timer EmuTimer;
	Apu EmuApu;

	void Run(int& currentCycle, int cycleTarget);

public:
	explicit Emulator(std::shared_ptr<Cartridge> cartridge);

	int* GetFrame();
	InputJoypad& GetJoypad() { return EmuJoypad; }
	MemoryMap& GetMemoryMap() { return EmuMemoryMap; }

	// Interleaved stereo 16-bit samples at the APU's sample rate, produced since the last call
	void TakeAudioSamples(std::vector<short>& out) { EmuApu.TakeSamples(out); }
	int GetAudioSampleRate() const { return EmuApu.GetSampleRate(); }
};

