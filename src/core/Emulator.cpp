#include "stdafx.h"
#include "Emulator.h"

void Emulator::Run(int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget)
	{
		auto cycles = EmuCpu.DoNextInstruction();

		// The cartridge's clock counts seconds, so it runs once per instruction rather than every machine cycle
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
	EmuGraphics.ResetFrame();

	// The last instruction of a frame can run past its end; those clocks come off this frame, so frames
	// average exactly FrameClocks and the emulated clock doesn't creep ahead of the frame rate
	auto cycleCounter = _frameOvershoot;
	auto cycleTarget = 0;

	for (auto i = 0; i < Graphics::VertPixels; i++)
	{
		EmuGraphics.SetLcdcStatus(LcdcStatus::OamReadMode);
		cycleTarget += Graphics::OamReadClocks;
		Run(cycleCounter, cycleTarget);

		// Drawn before mode 3 starts, so H-Blank writes only affect the next line, as on hardware
		EmuGraphics.DrawLine();

		EmuGraphics.SetLcdcStatus(LcdcStatus::OamAndVramReadMode);
		cycleTarget += Graphics::OamAndVramReadClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.SetLcdcStatus(LcdcStatus::HBlankMode);
		cycleTarget += Graphics::HBlankPeriodClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.EndLine();
	}

	EmuGraphics.SetLcdcStatus(LcdcStatus::VBlankMode);

	for (auto i = 0; i < Graphics::VBlankLines; i++)
	{
		cycleTarget += Graphics::ScanlineClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.RenderLine();
	}

	_frameOvershoot = cycleCounter - cycleTarget;
	return EmuGraphics.Bitmap;
}
