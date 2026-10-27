#include "stdafx.h"
#include "Emulator.h"
#include "CartridgeFactory.h"

void Emulator::Run(int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget)
	{
		auto cyclesToRun = std::min(cycleTarget - currentCycle, EmuTimer.GetCyclesToNextEvent());
		auto cyclesRun = 0;

		while (cyclesRun < cyclesToRun)
		{
			auto cycles = EmuCpu.DoNextInstruction();
			cyclesRun += cycles;
			EmuApu.RunCycles(cycles);

			// Timer register writes must see the current divider
			EmuTimer.RunCycles(cycles);
		}

		currentCycle += cyclesRun;
	}

	currentCycle = std::max(currentCycle, cycleTarget);
}

Emulator::Emulator(std::shared_ptr<Cartridge> cartridge)
{
	EmuTimer.SetCpu(&EmuCpu);
	EmuMemoryMap.SetTimer(&EmuTimer);
	EmuMemoryMap.SetApu(&EmuApu);
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
