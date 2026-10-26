#include "stdafx.h"
#include "Emulator.h"

void Emulator::RunCycles(int cycles)
{
	EmuTimer.RunCycles(cycles);
	// OAM DMA counts machine cycles
	for (auto cycle = 0; cycle < cycles; cycle += 4) EmuOamDma.RunCycle();
	EmuGraphics.RunCycles(cycles);
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
	EmuMemoryMap.SetOamDma(&EmuOamDma);
	EmuGraphics.SetOamDma(&EmuOamDma);
	EmuSerial.SetCpu(&EmuCpu);
	EmuMemoryMap.SetCartridge(cartridge);
	EmuJoypad.SetCpu(&EmuCpu);
}

int* Emulator::GetFrame()
{
	while (!EmuGraphics.TakeCompletedFrame())
	{
		auto cycles = EmuCpu.DoNextInstruction();

		// The cartridge's clock counts seconds, so it runs once per instruction rather than every machine cycle
		EmuCartridge->RunCycles(cycles);
	}

	return EmuGraphics.Bitmap;
}
