#include "stdafx.h"
#include "Emulator.h"
#include "CartridgeFactory.h"

void Emulator::Run(int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget)
	{
		currentCycle += EmuCpu.DoNextInstruction();
	}
}

void Emulator::RunCycles(int cycles)
{
	EmuTimer.RunCycles(cycles);
	EmuApu.RunCycles(cycles);
	EmuSerial.RunCycles(cycles);
}

Emulator::Emulator(std::shared_ptr<Cartridge> cartridge)
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
