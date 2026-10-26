#include "stdafx.h"
#include "Emulator.h"
#include "StateStream.h"

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
	while (!RunInstruction())
	{
	}

	return EmuGraphics.Bitmap;
}

bool Emulator::RunInstruction()
{
	auto cycles = EmuCpu.DoNextInstruction();

	// The cartridge's clock counts seconds, so it runs once per instruction rather than every machine cycle
	EmuCartridge->RunCycles(cycles);

	return EmuGraphics.TakeCompletedFrame();
}

void Emulator::WriteState(StateWriter& writer) const
{
	EmuCpu.WriteState(writer);
	EmuMemoryMap.WriteState(writer);
	EmuGraphics.WriteState(writer);
	EmuOamDma.WriteState(writer);
	EmuTimer.WriteState(writer);
	EmuApu.WriteState(writer);
	EmuSerial.WriteState(writer);
	EmuJoypad.WriteState(writer);
	EmuCartridge->WriteState(writer);
}

void Emulator::ReadState(StateReader& reader)
{
	EmuCpu.ReadState(reader);
	EmuMemoryMap.ReadState(reader);
	EmuGraphics.ReadState(reader);
	EmuOamDma.ReadState(reader);
	EmuTimer.ReadState(reader);
	EmuApu.ReadState(reader);
	EmuSerial.ReadState(reader);
	EmuJoypad.ReadState(reader);
	EmuCartridge->ReadState(reader);
}
