#include "stdafx.h"
#include "Emulator.h"
#include "StateStream.h"

void Emulator::RunCycles(int cpuClocks)
{
	// The timer, OAM DMA and serial port run on the CPU's clock, so at double speed they do too. The PPU and APU run on
	// the system clock, whose clocks last two of the CPU's at double speed
	EmuTimer.RunCycles(cpuClocks);
	EmuOamDma.RunCycle();
	auto systemClocks = cpuClocks >> static_cast<int>(EmuCpu.IsDoubleSpeed());
	EmuGraphics.RunCycles(systemClocks);
	EmuApu.RunCycles(systemClocks);
	EmuSerial.RunCycles(cpuClocks);
}

void Emulator::RunStoppedCycles(int systemClocks)
{
	// Only the APU's sample output keeps time of its own. Simplification: a serial transfer on an external clock
	// stands still too, though on hardware the other Game Boy could still clock it. The PPU stays where it stopped,
	// but frames keep coming for the front end
	EmuApu.RunStoppedCycles(systemClocks);
	EmuGraphics.RunStoppedCycles(systemClocks);
}

Emulator::Emulator(std::shared_ptr<Cartridge> cartridge) : Emulator(cartridge, ModelForRom(cartridge->GetRom()))
{
}

Emulator::Emulator(std::shared_ptr<Cartridge> cartridge, GameBoyModel model) : EmuCartridge{ cartridge }
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

	SetModel(model);
	if (model == GameBoyModel::Cgb) ApplyCgbPostBootState();
}

void Emulator::SetModel(GameBoyModel model)
{
	EmuModel = model;
	auto cgb = model == GameBoyModel::Cgb;

	// The CGB's VRAM DMA is connected only in CGB mode, so the DMG's instruction loop doesn't check it
	EmuCpu.SetVramDma(cgb ? &EmuVramDma : nullptr);
	EmuGraphics.SetVramDma(cgb ? &EmuVramDma : nullptr);
	EmuMemoryMap.SetVramDma(&EmuVramDma);
	EmuVramDma.SetCpu(&EmuCpu);
	EmuMemoryMap.SetCpu(&EmuCpu);

	EmuCpu.SetCgbMode(cgb);
	EmuMemoryMap.SetCgbMode(cgb);
	EmuGraphics.SetCgbMode(cgb);
	EmuOamDma.SetCgbMode(cgb);
	EmuApu.SetCgbMode(cgb);
	EmuSerial.SetCgbMode(cgb);
}

void Emulator::ApplyCgbPostBootState()
{
	// Simplifications: the boot ROM's logo is left out of VRAM, DIV starts as on the DMG at power-on rather than
	// with the count the boot ROM would leave (which depends on the header), and the LCD has just been turned on
	EmuCpu.ApplyCgbPostBootState();
	EmuMemoryMap.ApplyCgbPostBootState();
	EmuGraphics.ApplyCgbPostBootState();
	EmuOamDma.ApplyCgbPostBootState();
	EmuVramDma.ApplyCgbPostBootState();
	EmuApu.ApplyCgbPostBootState();
	EmuSerial.ApplyCgbPostBootState();
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

	// The cartridge's clock counts seconds, so it runs once per instruction rather than every machine cycle. It
	// has a crystal of its own, so it also keeps counting while STOP has the system clock stopped (when each
	// call to DoNextInstruction is one stopped machine cycle)
	EmuCartridge->RunCycles(cycles);

	return EmuGraphics.TakeCompletedFrame();
}

void Emulator::WriteState(StateWriter& writer) const
{
	writer.BeginSection("MODL");
	writer.WriteInt(static_cast<int>(EmuModel));
	writer.EndSection();

	EmuCpu.WriteState(writer);
	EmuMemoryMap.WriteState(writer);
	EmuGraphics.WriteState(writer);
	EmuOamDma.WriteState(writer);
	EmuTimer.WriteState(writer);
	EmuApu.WriteState(writer);
	EmuSerial.WriteState(writer);
	EmuJoypad.WriteState(writer);
	EmuCartridge->WriteState(writer);
	if (EmuModel == GameBoyModel::Cgb) EmuVramDma.WriteState(writer);
}

void Emulator::ReadState(StateReader& reader)
{
	auto model = GameBoyModel::Dmg;
	if (reader.FormatVersion() >= 3)
	{
		reader.BeginSection("MODL");
		model = reader.ReadEnum(GameBoyModel::Cgb);
		reader.EndSection();
	}

	// The components read their state in the model's mode
	SetModel(model);

	EmuCpu.ReadState(reader);
	EmuMemoryMap.ReadState(reader);
	EmuGraphics.ReadState(reader);
	EmuOamDma.ReadState(reader);
	EmuTimer.ReadState(reader);
	EmuApu.ReadState(reader);
	EmuSerial.ReadState(reader);
	EmuJoypad.ReadState(reader);
	EmuCartridge->ReadState(reader);

	if (model == GameBoyModel::Cgb)
	{
		EmuVramDma.ReadState(reader);
		if (EmuVramDma.IsCopying()) EmuCpu.RequestVramDma();
	}
	else
	{
		EmuVramDma.Reset();
	}
}
