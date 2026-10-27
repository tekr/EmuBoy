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
	std::shared_ptr<Cartridge> EmuCartridge;
	InputJoypad EmuJoypad;
	MemoryMap EmuMemoryMap { EmuJoypad };
	Cpu EmuCpu{ EmuMemoryMap };
	SpriteManager EmuSpriteManager;
	Graphics EmuGraphics{ EmuCpu, EmuMemoryMap, EmuSpriteManager };
	Timer EmuTimer;
	Apu EmuApu;
	Serial EmuSerial;

	int _frameOvershoot{ 0 };

	void Run(int& currentCycle, int cycleTarget);

	// The PPU runs on the system clock, so while STOP has stopped it, it doesn't change mode, count or render lines,
	// or raise interrupts, and on waking LY carries on from the line where it stopped. Simplification:
	// GetFrame's loop still keeps time, so the PPU's modes pick up wherever the loop has reached (in practice the
	// next frame's start, as the front end only changes keys between frames), out of step with LY until the next
	// frame starts it again from line 0. No DMG game uses STOP, and a per-machine-cycle PPU would remove this
	void SetPpuMode(LcdcStatus status);
	void DrawLine();
	void EndLine();
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

	// Clocks the CPU has run since power-on, including time halted or stopped
	uint64_t GetCpuClocks() const { return EmuCpu.GetTotalCycles(); }
	const Cpu& GetCpu() const { return EmuCpu; }

	// Interleaved stereo 16-bit samples at the APU's sample rate, produced since the last call
	void TakeAudioSamples(std::vector<short>& out) { EmuApu.TakeSamples(out); }
	int GetAudioSampleRate() const { return EmuApu.GetSampleRate(); }
};

