#include "stdafx.h"
#include "Emulator.h"

void Emulator::Run(int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget)
	{
		auto cycles = EmuCpu.DoNextInstruction();

		// The cartridge's clock counts seconds, so it runs once per instruction rather than every machine cycle. It
		// has a crystal of its own, so it also keeps counting while STOP has the system clock stopped (when each
		// call to DoNextInstruction is one stopped machine cycle)
		EmuCartridge->RunCycles(cycles);
		currentCycle += cycles;
	}
}

void Emulator::RunCycles(int cycles)
{
	EmuTimer.RunCycles(cycles);
	EmuApu.RunCycles(cycles);
	EmuSerial.RunCycles(cycles);
}

void Emulator::RunStoppedCycles(int cycles)
{
	// Only the APU's sample output keeps time of its own. Simplification: a serial transfer on an external clock
	// stands still too, though on hardware the other Game Boy could still clock it
	EmuApu.RunStoppedCycles(cycles);
}

void Emulator::SetPpuMode(LcdcStatus status)
{
	if (!EmuCpu.IsStopped()) EmuGraphics.SetLcdcStatus(status);
}

void Emulator::DrawLine()
{
	if (!EmuCpu.IsStopped()) EmuGraphics.DrawLine();
}

void Emulator::EndLine()
{
	if (!EmuCpu.IsStopped()) EmuGraphics.EndLine();
}

void Emulator::RenderLine()
{
	if (!EmuCpu.IsStopped()) EmuGraphics.RenderLine();
}

Emulator::Emulator(std::shared_ptr<Cartridge> cartridge) : EmuCartridge{ cartridge }
{
	EmuCpu.SetClockedHardware(this);
	EmuTimer.SetCpu(&EmuCpu);
	EmuMemoryMap.SetTimer(&EmuTimer);
	EmuMemoryMap.SetApu(&EmuApu);
	EmuMemoryMap.SetSerial(&EmuSerial);
	EmuSerial.SetCpu(&EmuCpu);
	EmuMemoryMap.SetCartridge(cartridge);
	EmuJoypad.SetCpu(&EmuCpu);
}

int* Emulator::GetFrame()
{
	// Resetting the frame sets LY and can raise the LY=LYC interrupt, so it waits while STOP has the PPU stopped
	if (!EmuCpu.IsStopped()) EmuGraphics.ResetFrame();

	// The last instruction of a frame can run past its end; those clocks come off this frame, so frames
	// average exactly FrameClocks and the emulated clock doesn't creep ahead of the frame rate
	auto cycleCounter = _frameOvershoot;
	auto cycleTarget = 0;

	for (auto i = 0; i < Graphics::VertPixels; i++)
	{
		SetPpuMode(LcdcStatus::OamReadMode);
		cycleTarget += Graphics::OamReadClocks;
		Run(cycleCounter, cycleTarget);

		// Drawn before mode 3 starts, so H-Blank writes only affect the next line, as on hardware
		DrawLine();

		SetPpuMode(LcdcStatus::OamAndVramReadMode);
		cycleTarget += Graphics::OamAndVramReadClocks;
		Run(cycleCounter, cycleTarget);

		SetPpuMode(LcdcStatus::HBlankMode);
		cycleTarget += Graphics::HBlankPeriodClocks;
		Run(cycleCounter, cycleTarget);

		EndLine();
	}

	SetPpuMode(LcdcStatus::VBlankMode);

	for (auto i = 0; i < Graphics::VBlankLines; i++)
	{
		cycleTarget += Graphics::ScanlineClocks;
		Run(cycleCounter, cycleTarget);

		RenderLine();
	}

	_frameOvershoot = cycleCounter - cycleTarget;

	// Per SameBoy (display.c), a DMG's screen is blank while stopped
	if (EmuCpu.IsStopped()) EmuGraphics.BlankScreen();

	return EmuGraphics.Bitmap;
}
