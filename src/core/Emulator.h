#pragma once
#include "Graphics.h"
#include "MemoryMap.h"
#include "SpriteManager.h"
#include "Cpu.h"
#include "Timer.h"
#include "Apu.h"
#include "Serial.h"
#include "OamDma.h"
#include <vector>

class Emulator : ClockedHardware
{
	std::shared_ptr<Cartridge> EmuCartridge;
	InputJoypad EmuJoypad;
	MemoryMap EmuMemoryMap { EmuJoypad };
	Cpu EmuCpu{ EmuMemoryMap };
	SpriteManager EmuSpriteManager;
	Graphics EmuGraphics{ EmuCpu, EmuMemoryMap, EmuSpriteManager };
	OamDma EmuOamDma{ EmuMemoryMap, EmuGraphics };
	Timer EmuTimer;
	Apu EmuApu;
	Serial EmuSerial;

	// Called by the CPU for each machine cycle
	void RunCycles(int cycles) override;

public:
	explicit Emulator(std::shared_ptr<Cartridge> cartridge);

	// Runs until the PPU completes a frame and returns it. Frames come as V-Blank starts, so they're FrameClocks
	// apart while the LCD stays on, and every FrameClocks while it's off
	int* GetFrame();
	InputJoypad& GetJoypad() { return EmuJoypad; }
	MemoryMap& GetMemoryMap() { return EmuMemoryMap; }
	Serial& GetSerial() { return EmuSerial; }

	// Clocks the CPU has run since power-on, including time halted or stopped
	uint64_t GetCpuClocks() const { return EmuCpu.GetTotalCycles(); }
	const Cpu& GetCpu() const { return EmuCpu; }

	// Interleaved stereo 16-bit samples at the APU's sample rate, produced since the last call
	void TakeAudioSamples(std::vector<short>& out) { EmuApu.TakeSamples(out); }
	int GetAudioSampleRate() const { return EmuApu.GetSampleRate(); }
};

