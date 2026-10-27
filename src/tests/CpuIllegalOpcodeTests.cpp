#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"
#include "../core/Emulator.h"
#include "../core/Timer.h"

// Per https://gbdev.io/pandocs/CPU_Instruction_Set.html, the unused opcodes $D3, $DB, $DD, $E3, $E4, $EB, $EC, $ED,
// $F4, $FC and $FD hard-lock the CPU until the console is powered off. Only the CPU stops: the rest of the hardware,
// such as the timer, keeps running on the system clock

namespace
{
	const int MachineCycle = 4;
	const unsigned short DivAddress = 0xff04;
	const unsigned short StackTop = 0xc010;

	const unsigned char IllegalOpcodes[] = { 0xd3, 0xdb, 0xdd, 0xe3, 0xe4, 0xeb, 0xec, 0xed, 0xf4, 0xfc, 0xfd };

	class TimerHardware : public ClockedHardware
	{
		Timer& _timer;

	public:
		explicit TimerHardware(Timer& timer) : _timer(timer) {}

		void RunCycles(int cycles) override { _timer.RunCycles(cycles); }
	};
}

class CpuIllegalOpcodeTests : public CpuTestFixture, public testing::WithParamInterface<unsigned char>
{
public:
	::Timer Timer;
	TimerHardware Hardware{ Timer };

	CpuIllegalOpcodeTests()
	{
		Timer.SetCpu(&Cpu);
		MemoryMap.SetTimer(&Timer);
		Cpu.SetClockedHardware(&Hardware);

		Cpu.Registers().PC = 0;
		Cpu.Registers().SP = StackTop;
		Cpu.InterruptsEnabled() = false;
		Cpu.EnabledInterrupts() = 0;
		Cpu.WaitingInterrupts() = 0;

		// The illegal opcode, then INC A, which must never run
		MemoryMap.SetBytes(0, { GetParam(), 0x3c, 0x3c, 0x3c });
	}

	void Step(int instructions)
	{
		for (auto i = 0; i < instructions; i++) Cpu.DoNextInstruction();
	}
};

TEST_P(CpuIllegalOpcodeTests, LocksTheCpuWithoutThrowing)
{
	EXPECT_NO_THROW(Cpu.DoNextInstruction());
	EXPECT_EQ(CpuState::Locked, Cpu.State());
}

TEST_P(CpuIllegalOpcodeTests, ExecutesNothingMore)
{
	Step(1);
	auto registers = Cpu.Registers();

	Step(100);
	EXPECT_EQ(registers, Cpu.Registers());
	EXPECT_EQ(CpuState::Locked, Cpu.State());
}

TEST_P(CpuIllegalOpcodeTests, InterruptsDontWakeIt)
{
	Step(1);
	auto registers = Cpu.Registers();

	Cpu.InterruptsEnabled() = true;
	Cpu.EnabledInterrupts() = InterruptFlags::AllInt;
	Cpu.RequestInterrupt(InterruptFlags::VBlankInt);
	Cpu.RequestInterrupt(InterruptFlags::JoypadInt);

	Step(100);
	EXPECT_EQ(registers, Cpu.Registers());
	EXPECT_EQ(InterruptFlags::VBlankInt | InterruptFlags::JoypadInt, Cpu.WaitingInterrupts());
	EXPECT_EQ(CpuState::Locked, Cpu.State());
}

TEST_P(CpuIllegalOpcodeTests, OtherHardwareKeepsRunning)
{
	Step(1);
	MemoryMap.WriteByte(DivAddress, 0);

	// DIV increments every 64 machine cycles
	auto cycles = 0;
	for (auto i = 0; i < 64; i++) cycles += Cpu.DoNextInstruction();

	EXPECT_EQ(64 * MachineCycle, cycles);
	EXPECT_EQ(1, MemoryMap.ReadByte(DivAddress));
}

INSTANTIATE_TEST_CASE_P(UnusedOpcodes, CpuIllegalOpcodeTests, ::testing::ValuesIn(IllegalOpcodes));

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
	auto div = emulator.GetMemoryMap().ReadByte(DivAddress);
	emulator.GetFrame();
	EXPECT_NE(div, emulator.GetMemoryMap().ReadByte(DivAddress));
}
