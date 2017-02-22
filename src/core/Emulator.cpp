#include "stdafx.h"
#include "Emulator.h"
#include "CartridgeFactory.h"

void Emulator::Run(int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget)
	{
		auto cyclesToRun = std::min(cycleTarget - currentCycle, EmuTimer.GetCyclesToNextEvent());
		auto cyclesRun = 0;

		while (cyclesRun < cyclesToRun) cyclesRun += EmuCpu.DoNextInstruction();

		EmuTimer.RunCycles(cyclesRun);
		currentCycle += cyclesRun;
	}

	currentCycle = std::max(currentCycle, cycleTarget);
}

Emulator::Emulator(std::shared_ptr<Cartridge> cartridge)
{
	EmuTimer.SetCpu(&EmuCpu);
	EmuMemoryMap.SetTimer(&EmuTimer);
	EmuMemoryMap.SetCartridge(cartridge);
}

Emulator::~Emulator()
{
}

int* Emulator::GetFrame()
{
	EmuGraphics.ResetFrame();
	auto cycleCounter = 0;
	auto cycleTarget = 0;

	for (auto i = 0; i < Graphics::VertPixels; i++)
	{
		EmuGraphics.SetLcdcStatus(LcdcStatus::OamReadMode);
		cycleTarget += Graphics::OamReadClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.SetLcdcStatus(LcdcStatus::OamAndVramReadMode);
		cycleTarget += Graphics::OamAndVramReadClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.SetLcdcStatus(LcdcStatus::HBlankMode);
		cycleTarget += Graphics::HBlankPeriodClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.RenderLine();
	}

	EmuGraphics.SetLcdcStatus(LcdcStatus::VBlankMode);

	for (auto i = 0; i < Graphics::VBlankLines; i++)
	{
		cycleTarget += Graphics::ScanlineClocks;
		Run(cycleCounter, cycleTarget);

		EmuGraphics.RenderLine();
	}

	return EmuGraphics.Bitmap;
}
