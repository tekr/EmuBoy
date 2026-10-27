#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"
#include "../core/Emulator.h"
#include "TimerTestHardware.h"

// Per https://gbdev.io/pandocs/CPU_Instruction_Set.html, the unused opcodes $D3, $DB, $DD, $E3, $E4, $EB, $EC, $ED,
// $F4, $FC and $FD hard-lock the CPU until the console is powered off. Only the CPU stops: the rest of the hardware,
// such as the timer, keeps running on the system clock

namespace
{
	const unsigned char IllegalOpcodes[] = { 0xd3, 0xdb, 0xdd, 0xe3, 0xe4, 0xeb, 0xec, 0xed, 0xf4, 0xfc, 0xfd };
}

class CpuIllegalOpcodeTests : public CpuTestFixture, public testing::WithParamInterface<unsigned char>
{
public:
	Timer TimerUnit;
	TimerTestHardware Hardware{ TimerUnit };

	CpuIllegalOpcodeTests()
	{
		TimerUnit.SetCpu(&Processor);
		Memory.SetTimer(&TimerUnit);
		Processor.SetClockedHardware(&Hardware);

		Processor.RegisterFile().PC = 0;
		Processor.RegisterFile().SP = StackTop;
		Processor.InterruptsEnabled() = false;
		Processor.EnabledInterrupts() = 0;
		Processor.WaitingInterrupts() = 0;

		// The illegal opcode, then INC A, which must never run
		Memory.SetBytes(0, { GetParam(), 0x3c, 0x3c, 0x3c });
	}
};

TEST_P(CpuIllegalOpcodeTests, LocksTheCpuWithoutThrowing)
{
	EXPECT_NO_THROW(Processor.DoNextInstruction());
	EXPECT_EQ(CpuState::Locked, Processor.State());
}

TEST_P(CpuIllegalOpcodeTests, ExecutesNothingMore)
{
	Step(1);
	auto registers = Processor.RegisterFile();

	Step(100);
	EXPECT_EQ(registers, Processor.RegisterFile());
	EXPECT_EQ(CpuState::Locked, Processor.State());
}

TEST_P(CpuIllegalOpcodeTests, InterruptsDontWakeIt)
{
	Step(1);
	auto registers = Processor.RegisterFile();

	Processor.InterruptsEnabled() = true;
	Processor.EnabledInterrupts() = InterruptFlags::AllInt;
	Processor.RequestInterrupt(InterruptFlags::VBlankInt);
	Processor.RequestInterrupt(InterruptFlags::JoypadInt);

	Step(100);
	EXPECT_EQ(registers, Processor.RegisterFile());
	EXPECT_EQ(InterruptFlags::VBlankInt | InterruptFlags::JoypadInt, Processor.WaitingInterrupts());
	EXPECT_EQ(CpuState::Locked, Processor.State());
}

TEST_P(CpuIllegalOpcodeTests, OtherHardwareKeepsRunning)
{
	Step(1);
	Memory.WriteByte(MemoryMap::DividerPort, 0);

	// DIV increments every 64 machine cycles
	auto cycles = 0;
	for (auto i = 0; i < 64; i++) cycles += Processor.DoNextInstruction();

	EXPECT_EQ(64 * MachineCycle, cycles);
	EXPECT_EQ(1, Memory.ReadByte(MemoryMap::DividerPort));
}

INSTANTIATE_TEST_CASE_P(UnusedOpcodes, CpuIllegalOpcodeTests, ::testing::ValuesIn(IllegalOpcodes));

TEST(EmulatorIllegalOpcodeTests, EachFrameLastsAFrameOfClocksAfterLockingUp)
{
	// The front end paces frames by the clocks emulated, so a locked-up Game Boy must keep the normal frame rate
	auto rom = std::make_shared<TestCartridge>();
	rom->SetBytes(0, { 0x3e, 0x91, 0xe0, 0x40, 0xd3 });	// LD A,$91; LDH (LCDC),A; then an unused opcode
	Emulator emulator{ rom };
	emulator.GetMemoryMap().WriteByte(0xff50, 1);
	emulator.GetFrame();
	emulator.GetFrame();

	for (auto i = 0; i < 3; i++)
	{
		auto clocks = emulator.GetCpuClocks();
		emulator.GetFrame();
		EXPECT_EQ(uint64_t{ Graphics::FrameClocks }, emulator.GetCpuClocks() - clocks);
	}
}

TEST(EmulatorIllegalOpcodeTests, KeepsProducingFramesAndSoundAfterLockingUp)
{
	auto rom = std::make_shared<TestCartridge>();
	rom->SetBytes(0, { 0xd3 });
	Emulator emulator{ rom };
	emulator.GetMemoryMap().WriteByte(0xff50, 1);

	std::vector<short> samples;
	for (auto i = 0; i < 3; i++)
	{
		EXPECT_NO_THROW(emulator.GetFrame());
		emulator.TakeAudioSamples(samples);
		EXPECT_FALSE(samples.empty());
	}

	// DIV is still counting
	auto div = emulator.GetMemoryMap().ReadByte(MemoryMap::DividerPort);
	emulator.GetFrame();
	EXPECT_NE(div, emulator.GetMemoryMap().ReadByte(MemoryMap::DividerPort));
}
