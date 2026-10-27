#pragma once
#include "Graphics.h"
#include "MemoryMap.h"
#include "SpriteManager.h"
#include "Cpu.h"
#include "Timer.h"
#include "Apu.h"
#include "Serial.h"
#include <vector>

class Emulator : ClockedHardware
{
	InputJoypad EmuJoypad;
	MemoryMap EmuMemoryMap { EmuJoypad };
	Cpu EmuCpu{ EmuMemoryMap };
	SpriteManager EmuSpriteManager;
	Graphics EmuGraphics{ EmuCpu, EmuMemoryMap, EmuSpriteManager };
	Timer EmuTimer;
	Apu EmuApu;
	Serial EmuSerial;

	void Run(int& currentCycle, int cycleTarget);

	// The PPU runs on the system clock, so while STOP has stopped it, it doesn't change mode, render or raise
	// interrupts. Simplification: GetFrame's loop still keeps time, so on waking the PPU picks up wherever the loop
	// has reached (in practice the next frame's start, as the front end only changes keys between frames) rather
	// than resuming from where it stopped. Until the loop's next mode change or line end, LY and STAT still read
	// what they did when the CPU stopped, after which LY restarts from the loop's line rather than carrying on. No
	// DMG game uses STOP, and a per-machine-cycle PPU would remove this
	void SetPpuMode(LcdcStatus status);
	void RenderLine();

	// Called by the CPU for each machine cycle
	void RunCycles(int cycles) override;
	void RunStoppedCycles(int cycles) override;

public:
	explicit Emulator(std::shared_ptr<Cartridge> cartridge);

	int* GetFrame();
	InputJoypad& GetJoypad() { return EmuJoypad; }
	MemoryMap& GetMemoryMap() { return EmuMemoryMap; }
	Serial& GetSerial() { return EmuSerial; }

	// Interleaved stereo 16-bit samples at the APU's sample rate, produced since the last call
	void TakeAudioSamples(std::vector<short>& out) { EmuApu.TakeSamples(out); }
	int GetAudioSampleRate() const { return EmuApu.GetSampleRate(); }
};

