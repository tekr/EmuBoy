#pragma once
#include "Graphics.h"
#include "MemoryMap.h"
#include "SpriteManager.h"
#include "Cpu.h"
#include "Timer.h"
#include "Apu.h"
#include "Serial.h"
#include "OamDma.h"
#include "GameBoyModel.h"
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

	GameBoyModel EmuModel{ GameBoyModel::Dmg };

	// Called by the CPU for each machine cycle
	void RunCycles(int cycles) override;
	void RunStoppedCycles(int cycles) override;

	// Puts every component in the model's mode
	void SetModel(GameBoyModel model);

	// Starts a CGB game where the CGB boot ROM would hand over to it, as Emu Boy has no CGB boot ROM
	void ApplyCgbPostBootState();

public:
	// Runs the game on the model its header asks for (see ModelForRom)
	explicit Emulator(std::shared_ptr<Cartridge> cartridge);

	// Runs the game on the given model. The DMG starts with its boot ROM; the CGB starts after its boot ROM would
	// have handed over, in the state Pan Docs documents
	Emulator(std::shared_ptr<Cartridge> cartridge, GameBoyModel model);

	// The components point into each other, so an emulator can't be copied or moved
	Emulator(const Emulator&) = delete;
	Emulator& operator=(const Emulator&) = delete;

	GameBoyModel GetModel() const { return EmuModel; }

	// Runs until the PPU completes a frame and returns it. Frames come as V-Blank starts, so they're FrameClocks
	// apart while the LCD stays on, and every FrameClocks while it's off. Turning the LCD on moves the boundary: the
	// next frame comes at its first V-Blank, so the frame it's turned on in lasts up to nearly two frames. While STOP
	// has the system clock stopped, blank frames come every FrameClocks, and waking does the same as turning the LCD
	// on: the frame it wakes in lasts until the PPU reaches V-Blank, from a machine cycle to nearly two frames. Pace
	// by GetSystemClocks rather than by frames
	int* GetFrame();

	// The frame as drawn so far, which between calls to GetFrame is the frame it returned
	const int* GetScreen() const { return EmuGraphics.Bitmap; }

	// Runs one instruction, interrupt dispatch or halted machine cycle. Returns true if that completed a frame, which
	// GetFrame would have returned then. For stopping between frames, e.g. to save a state there
	bool RunInstruction();

	InputJoypad& GetJoypad() { return EmuJoypad; }
	MemoryMap& GetMemoryMap() { return EmuMemoryMap; }
	Serial& GetSerial() { return EmuSerial; }
	const OamDma& GetOamDma() const { return EmuOamDma; }

	// Clocks of the 4 MHz system clock since power-on, including time halted or stopped
	uint64_t GetSystemClocks() const { return EmuCpu.GetTotalSystemClocks(); }
	const Cpu& GetCpu() const { return EmuCpu; }

	// Interleaved stereo 16-bit samples at the APU's sample rate, produced since the last call
	void TakeAudioSamples(std::vector<short>& out) { EmuApu.TakeSamples(out); }
	int GetAudioSampleRate() const { return EmuApu.GetSampleRate(); }

	const Cartridge& GetCartridge() const { return *EmuCartridge; }

	// Every component's state, for save states (see SaveState, which adds the header and checks). Called between
	// instructions. Reading can fail part-way with StateError, leaving the emulator part-restored. A state says
	// which model it's for, and loading it switches the emulator to that model; a version 2 state is for the DMG
	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);
};

