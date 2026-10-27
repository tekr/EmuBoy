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

void Emulator::RunStoppedCycles(int cycles)
{
	// Only the APU's sample output keeps time of its own. Simplification: a serial transfer on an external clock
	// stands still too, though on hardware the other Game Boy could still clock it. The PPU stays where it stopped,
	// but frames keep coming for the front end
	EmuApu.RunStoppedCycles(cycles);
	EmuGraphics.RunStoppedCycles(cycles);
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

		// The cartridge's clock counts seconds, so it runs once per instruction rather than every machine cycle. It
		// has a crystal of its own, so it also keeps counting while STOP has the system clock stopped (when each
		// call to DoNextInstruction is one stopped machine cycle)
		EmuCartridge->RunCycles(cycles);
	}

	return EmuGraphics.Bitmap;
}
