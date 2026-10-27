#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"
#include "TimerTestHardware.h"

// Runs the CPU with the timer as its clocked hardware, so timer register accesses happen at the machine cycle the
// instruction makes them. Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html, TIMA increments on the
// falling edge of the selected divider bit, and after overflowing reads 0 for a machine cycle (cycle A) before
// being reloaded from TMA and requesting the interrupt (cycle B).

namespace
{
	const unsigned short TimaAddress = 0xff05;
	const unsigned short TmaAddress = 0xff06;
	const unsigned short TacAddress = 0xff07;

	// Enabled, 16 cycles per increment (divider bit 3)
	const unsigned char TacMode1 = 0x05;
}

class CpuTimerTests : public CpuTestFixture
{
public:
	Timer DividerTimer;
	TimerTestHardware Hardware{ DividerTimer };

	CpuTimerTests()
	{
		DividerTimer.SetCpu(&Processor);
		Memory.SetTimer(&DividerTimer);
		Processor.SetClockedHardware(&Hardware);

		Processor.GetRegisters().PC = 0;
		Processor.GetRegisters().SP = StackTop;
		Processor.InterruptsEnabled() = false;
		Processor.EnabledInterrupts() = InterruptFlags::TimerInt;
		Processor.WaitingInterrupts() = 0;

		// Divider at 0, so TIMA next increments (and overflows) at the end of the 4th machine cycle
		Memory.WriteByte(MemoryMap::DividerPort, 0);
		Memory.WriteByte(TmaAddress, 0xf0);
		Memory.WriteByte(TimaAddress, 0xff);
		Memory.WriteByte(TacAddress, TacMode1);
	}

	unsigned char Tima() { return Memory.ReadByte(TimaAddress); }
	bool TimerInterruptRequested() { return (Processor.WaitingInterrupts() & InterruptFlags::TimerInt) != 0; }
};

TEST_F(CpuTimerTests, InterruptRequestedDuringOpcodeFetchIsDispatchedInsteadOfTheInstruction)
{
	// Disabling the timer in the third cycle of LDH, with divider bit 3 set (divider 12), overflows TIMA. The
	// interrupt is requested a cycle later, during the next opcode fetch, which still sees it
	Memory.SetBytes(MemoryMap::RomFixed, { 0xe0, 0x07, 0x3c });	// LDH (TAC),A; INC A
	Processor.GetRegisters().A = 0x01;
	Processor.InterruptsEnabled() = true;

	Step(1);
	EXPECT_EQ(0, Tima());
	EXPECT_FALSE(TimerInterruptRequested());

	Step(1);
	EXPECT_EQ(0x50, Processor.GetRegisters().PC);
	EXPECT_EQ(0x01, Processor.GetRegisters().A);
	EXPECT_EQ(0x02, Memory.ReadByte(StackTop - 2));
	EXPECT_EQ(0xf0, Tima());
}

// In the programs below the divider starts at 0, so TIMA overflows at the end of the 4th machine cycle. An LDH
// after a NOP writes in that cycle (cycle A); after two NOPs it writes in the next (cycle B)

TEST_F(CpuTimerTests, TimaWriteInCycleACancelsReloadAndInterrupt)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0x00, 0xe0, 0x05, 0x00 });	// NOP; LDH (TIMA),A; NOP
	Processor.GetRegisters().A = 0x42;

	Step(3);
	EXPECT_EQ(0x42, Tima());
	EXPECT_FALSE(TimerInterruptRequested());
}

TEST_F(CpuTimerTests, TmaWriteInCycleAIsReloaded)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0x00, 0xe0, 0x06, 0x00 });	// NOP; LDH (TMA),A; NOP
	Processor.GetRegisters().A = 0x37;

	Step(3);
	EXPECT_EQ(0x37, Tima());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(CpuTimerTests, TimaWriteInCycleBIsIgnored)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0x00, 0x00, 0xe0, 0x05 });	// NOP; NOP; LDH (TIMA),A
	Processor.GetRegisters().A = 0x42;

	Step(3);
	EXPECT_EQ(0xf0, Tima());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(CpuTimerTests, TmaWriteInCycleBAlsoLoadsTima)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0x00, 0x00, 0xe0, 0x06 });	// NOP; NOP; LDH (TMA),A
	Processor.GetRegisters().A = 0x37;

	Step(3);
	EXPECT_EQ(0x37, Tima());
}

TEST_F(CpuTimerTests, DisablingTimerInCycleADoesNotStopReload)
{
	// Divider bit 3 is clear at 16, so disabling causes no extra increment
	Memory.SetBytes(MemoryMap::RomFixed, { 0x00, 0xe0, 0x07, 0x00 });	// NOP; LDH (TAC),A; NOP
	Processor.GetRegisters().A = 0x01;

	Step(3);
	EXPECT_EQ(0xf0, Tima());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(CpuTimerTests, DivWriteInCycleBKeepsReloadAndRestartsPeriod)
{
	// Divider 20 has bit 3 clear, so the reset causes no extra increment. The next comes 16 cycles later
	Memory.SetBytes(MemoryMap::RomFixed, { 0x00, 0x00, 0xe0, 0x04, 0x00, 0x00, 0x00, 0x00 });

	Step(3);
	EXPECT_EQ(0xf0, Tima());
	EXPECT_TRUE(TimerInterruptRequested());

	Step(3);
	EXPECT_EQ(0xf0, Tima());

	Step(1);
	EXPECT_EQ(0xf1, Tima());
}

TEST_F(CpuTimerTests, EarliestTimaWriteAfterWriteTriggeredOverflowFollowsTheReload)
{
	// LDH (TAC),A overflows TIMA in its last cycle (divider 12, bit 3 set); the reload happens during the next opcode fetch and its
	// cycle ends with the operand read, so LD (HL),d8 writes after both
	Memory.SetBytes(MemoryMap::RomFixed, { 0xe0, 0x07, 0x36, 0x42 });	// LDH (TAC),A; LD (HL),$42
	Processor.GetRegisters().A = 0x01;
	Processor.GetRegisters().HL = TimaAddress;

	Step(2);
	EXPECT_EQ(0x42, Tima());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(CpuTimerTests, EarliestTmaWriteAfterWriteTriggeredOverflowDoesNotLoadTima)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0xe0, 0x07, 0x36, 0x42 });	// LDH (TAC),A; LD (HL),$42
	Processor.GetRegisters().A = 0x01;
	Processor.GetRegisters().HL = TmaAddress;

	Step(2);
	EXPECT_EQ(0xf0, Tima());
	EXPECT_EQ(0x42, Memory.ReadByte(TmaAddress));
}
