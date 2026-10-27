#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"

// Machine-cycle timing of memory accesses, interrupt dispatch, EI and HALT. Expectations come from
// https://gbdev.io/gb-opcodes/optables/, https://gbdev.io/pandocs/Interrupts.html and https://gbdev.io/pandocs/halt.html

namespace
{
	const unsigned short CounterAddress = 0xff80;

	// Counts machine cycles into a byte of high RAM, so an instruction that reads it sees the number of the cycle
	// it reads in (the access is made at the end of its cycle), and a value written there is only incremented by
	// the cycles that follow the write
	class CycleCountingHardware : public ClockedHardware
	{
		MemoryMap& _memoryMap;

	public:
		explicit CycleCountingHardware(MemoryMap& memoryMap) : _memoryMap(memoryMap) {}

		void RunCycles(int cycles) override
		{
			_memoryMap.WriteByte(CounterAddress, static_cast<unsigned char>(_memoryMap.ReadByte(CounterAddress) + cycles / CpuTestFixture::MachineCycle));
		}
	};
}

class CpuTimingTests : public CpuTestFixture
{
public:
	CycleCountingHardware Hardware{ Memory };

	CpuTimingTests()
	{
		Processor.SetClockedHardware(&Hardware);
		Processor.RegisterFile().PC = 0;
		Processor.RegisterFile().SP = StackTop;
		Processor.InterruptsEnabled() = false;
		Processor.EnabledInterrupts() = 0;
		Processor.WaitingInterrupts() = 0;
	}

	void RunProgram(std::vector<unsigned char>&& program, unsigned short address = MemoryMap::RomFixed)
	{
		Memory.SetBytes(address, std::move(program));
		Processor.RegisterFile().PC = address;
		Memory.WriteByte(CounterAddress, 0);
		Cycles = Processor.DoNextInstruction();
	}

	unsigned char Counter() { return Memory.ReadByte(CounterAddress); }

	int Cycles = 0;
};

TEST_F(CpuTimingTests, ReadsAreMadeInTheLastMachineCycle)
{
	const std::vector<std::pair<std::vector<unsigned char>, int>> reads =
	{
		{ { 0x7e },				2 },	// LD A,(HL)
		{ { 0x2a },				2 },	// LD A,(HL+)
		{ { 0xf2 },				2 },	// LD A,(C)
		{ { 0xf0, 0x80 },		3 },	// LDH A,(a8)
		{ { 0xfa, 0x80, 0xff },	4 },	// LD A,(a16)
	};

	for (auto& read : reads)
	{
		SCOPED_TRACE(static_cast<int>(read.first[0]));
		Processor.RegisterFile().HL = CounterAddress;
		Processor.RegisterFile().C = CounterAddress & 0xff;
		Processor.RegisterFile().PC = 0;

		auto program = read.first;
		RunProgram(std::move(program));

		EXPECT_EQ(read.second, Processor.RegisterFile().A);
		EXPECT_EQ(read.second * MachineCycle, Cycles);
	}
}

TEST_F(CpuTimingTests, WritesAreMadeInTheLastMachineCycle)
{
	const std::vector<std::pair<std::vector<unsigned char>, int>> writes =
	{
		{ { 0x77 },				2 },	// LD (HL),A
		{ { 0x22 },				2 },	// LD (HL+),A
		{ { 0x36, 0x40 },		3 },	// LD (HL),d8
		{ { 0xe2 },				2 },	// LD (C),A
		{ { 0xe0, 0x80 },		3 },	// LDH (a8),A
		{ { 0xea, 0x80, 0xff },	4 },	// LD (a16),A
	};

	for (auto& write : writes)
	{
		SCOPED_TRACE(static_cast<int>(write.first[0]));
		Processor.RegisterFile().HL = CounterAddress;
		Processor.RegisterFile().C = CounterAddress & 0xff;
		Processor.RegisterFile().A = 0x40;
		Processor.RegisterFile().PC = 0;

		auto program = write.first;
		RunProgram(std::move(program));

		// No cycles followed the write to increment the value written
		EXPECT_EQ(0x40, Counter());
		EXPECT_EQ(write.second * MachineCycle, Cycles);
	}
}

TEST_F(CpuTimingTests, ReadModifyWriteReadsInTheNextToLastCycleAndWritesInTheLast)
{
	// DEC (HL): reads 2 in cycle 2, writes 1 in cycle 3
	Processor.RegisterFile().HL = CounterAddress;
	RunProgram({ 0x35 });
	EXPECT_EQ(1, Counter());
	EXPECT_EQ(3 * MachineCycle, Cycles);

	// SWAP (HL): reads 3 in cycle 3, writes 0x30 in cycle 4
	Processor.RegisterFile().PC = 0;
	RunProgram({ 0xcb, 0x36 });
	EXPECT_EQ(0x30, Counter());
	EXPECT_EQ(4 * MachineCycle, Cycles);
}

TEST_F(CpuTimingTests, BitTestOfMemoryReadsInTheLastCycle)
{
	// BIT 0,(HL) and BIT 1,(HL) read 3 (binary 11) in cycle 3; in cycle 2 bit 0 would be clear
	Processor.RegisterFile().HL = CounterAddress;
	RunProgram({ 0xcb, 0x46 });
	EXPECT_EQ(0, Processor.RegisterFile().F & ZeroFlag);
	EXPECT_EQ(3 * MachineCycle, Cycles);
}

// Stack accesses: pointing SP at the counter shows which cycle each byte is pushed or popped in

TEST_F(CpuTimingTests, PushWritesHighByteInCycleThreeAndLowByteInCycleFour)
{
	Processor.RegisterFile().BC = 0x1234;

	// The high byte lands on the counter with one cycle still to run
	Processor.RegisterFile().SP = CounterAddress + 1;
	RunProgram({ 0xc5 });
	EXPECT_EQ(0x13, Counter());
	EXPECT_EQ(4 * MachineCycle, Cycles);

	Processor.RegisterFile().SP = CounterAddress + 2;
	RunProgram({ 0xc5 });
	EXPECT_EQ(0x34, Counter());
}

TEST_F(CpuTimingTests, RstPushesInCyclesThreeAndFour)
{
	// Returns to 0x0231
	Processor.RegisterFile().SP = CounterAddress + 1;
	RunProgram({ 0xcf }, 0x0230);
	EXPECT_EQ(0x03, Counter());
	EXPECT_EQ(4 * MachineCycle, Cycles);
	EXPECT_EQ(0x08, Processor.RegisterFile().PC);

	Processor.RegisterFile().SP = CounterAddress + 2;
	RunProgram({ 0xcf }, 0x0230);
	EXPECT_EQ(0x31, Counter());
}

TEST_F(CpuTimingTests, CallPushesInCyclesFiveAndSix)
{
	// CALL and CALL NZ (taken, as Z is clear) return to 0x0233
	for (unsigned char opcode : { 0xcd, 0xc4 })
	{
		SCOPED_TRACE(static_cast<int>(opcode));
		Processor.RegisterFile().F = NoFlags;

		Processor.RegisterFile().SP = CounterAddress + 1;
		RunProgram({ opcode, 0x00, 0x10 }, 0x0230);
		EXPECT_EQ(0x03, Counter());
		EXPECT_EQ(6 * MachineCycle, Cycles);
		EXPECT_EQ(0x1000, Processor.RegisterFile().PC);

		Processor.RegisterFile().SP = CounterAddress + 2;
		RunProgram({ opcode, 0x00, 0x10 }, 0x0230);
		EXPECT_EQ(0x33, Counter());
	}

	// CALL NZ not taken
	Processor.RegisterFile().F = ZeroFlag;
	Processor.RegisterFile().SP = CounterAddress + 2;
	RunProgram({ 0xc4, 0x00, 0x10 }, 0x0230);
	EXPECT_EQ(3 * MachineCycle, Cycles);
	EXPECT_EQ(0x0233, Processor.RegisterFile().PC);
}

TEST_F(CpuTimingTests, ReturnPopsLowByteAfterAnyConditionCheck)
{
	Memory.WriteByte(CounterAddress + 1, 0x12);

	// RET: low byte popped in cycle 2
	Processor.RegisterFile().SP = CounterAddress;
	RunProgram({ 0xc9 });
	EXPECT_EQ(0x1202, Processor.RegisterFile().PC);
	EXPECT_EQ(4 * MachineCycle, Cycles);

	// RET NZ taken: a cycle to check the condition first
	Processor.RegisterFile().F = NoFlags;
	Processor.RegisterFile().SP = CounterAddress;
	RunProgram({ 0xc0 });
	EXPECT_EQ(0x1203, Processor.RegisterFile().PC);
	EXPECT_EQ(5 * MachineCycle, Cycles);

	// RET NZ not taken
	Processor.RegisterFile().F = ZeroFlag;
	Processor.RegisterFile().SP = CounterAddress;
	RunProgram({ 0xc0 });
	EXPECT_EQ(1, Processor.RegisterFile().PC);
	EXPECT_EQ(2 * MachineCycle, Cycles);
}

TEST_F(CpuTimingTests, PopReadsLowByteInCycleTwo)
{
	Memory.WriteByte(CounterAddress + 1, 0x12);
	Processor.RegisterFile().SP = CounterAddress;
	RunProgram({ 0xc1 });

	EXPECT_EQ(0x1202, Processor.RegisterFile().BC);
	EXPECT_EQ(3 * MachineCycle, Cycles);
}

TEST_F(CpuTimingTests, StoreSpWritesLowByteInCycleFourAndHighByteInCycleFive)
{
	Processor.RegisterFile().SP = 0xabcd;
	RunProgram({ 0x08, 0x80, 0xff });
	EXPECT_EQ(0xce, Counter());
	EXPECT_EQ(5 * MachineCycle, Cycles);

	RunProgram({ 0x08, 0x7f, 0xff });
	EXPECT_EQ(0xab, Counter());
}

TEST_F(CpuTimingTests, StackPointerArithmeticEndsWithInternalCycles)
{
	// ADD SP,e: fetch, operand, two internal cycles. LD HL,SP+e: fetch, operand, one internal cycle
	RunProgram({ 0xe8, 0x01 });
	EXPECT_EQ(4 * MachineCycle, Cycles);
	EXPECT_EQ(StackTop + 1, Processor.RegisterFile().SP);

	RunProgram({ 0xf8, 0x01 });
	EXPECT_EQ(3 * MachineCycle, Cycles);
	EXPECT_EQ(StackTop + 2, Processor.RegisterFile().HL);
}

TEST_F(CpuTimingTests, InterruptDispatchTakesFiveMachineCycles)
{
	Processor.InterruptsEnabled() = true;
	Processor.EnabledInterrupts() = InterruptFlags::TimerInt;
	Processor.RequestInterrupt(InterruptFlags::TimerInt);
	Processor.RegisterFile().PC = 0x1234;

	EXPECT_EQ(5 * MachineCycle, Processor.DoNextInstruction());
	EXPECT_EQ(0x50, Processor.RegisterFile().PC);
	EXPECT_EQ(0, Processor.WaitingInterrupts());
	EXPECT_FALSE(Processor.InterruptsEnabled());
	EXPECT_EQ(0x34, Memory.ReadByte(StackTop - 2));
	EXPECT_EQ(0x12, Memory.ReadByte(StackTop - 1));
}

TEST_F(CpuTimingTests, HighestPriorityInterruptIsDispatchedFirst)
{
	Processor.InterruptsEnabled() = true;
	Processor.EnabledInterrupts() = InterruptFlags::AllInt;
	Processor.RequestInterrupt(static_cast<InterruptFlags>(InterruptFlags::SerialInt | InterruptFlags::LcdStatInt));

	Processor.DoNextInstruction();
	EXPECT_EQ(0x48, Processor.RegisterFile().PC);
	EXPECT_EQ(InterruptFlags::SerialInt, Processor.WaitingInterrupts());
}

TEST_F(CpuTimingTests, InterruptIsChosenAfterPushingHighByteOfPC)
{
	// With SP at 0, the high byte of PC is pushed to IE at 0xffff
	Processor.InterruptsEnabled() = true;
	Processor.RegisterFile().SP = 0;

	// The push leaves only the timer interrupt enabled
	Processor.RegisterFile().PC = InterruptFlags::TimerInt << 8;
	Processor.EnabledInterrupts() = InterruptFlags::VBlankInt;
	Processor.RequestInterrupt(static_cast<InterruptFlags>(InterruptFlags::VBlankInt | InterruptFlags::TimerInt));
	Processor.DoNextInstruction();
	EXPECT_EQ(0x50, Processor.RegisterFile().PC);

	// The push disables the only pending interrupt, so the CPU jumps to 0 and the interrupt stays pending
	Processor.InterruptsEnabled() = true;
	Processor.RegisterFile().SP = 0;
	Processor.RegisterFile().PC = 0x0200;
	Processor.EnabledInterrupts() = InterruptFlags::VBlankInt;
	Processor.WaitingInterrupts() = InterruptFlags::VBlankInt;
	Processor.DoNextInstruction();
	EXPECT_EQ(0, Processor.RegisterFile().PC);
	EXPECT_EQ(InterruptFlags::VBlankInt, Processor.WaitingInterrupts());
}

class CpuInterruptEnableTests : public CpuTimingTests
{
public:
	CpuInterruptEnableTests()
	{
		Processor.EnabledInterrupts() = InterruptFlags::VBlankInt;
		Processor.RequestInterrupt(InterruptFlags::VBlankInt);
	}
};

TEST_F(CpuInterruptEnableTests, EiTakesEffectAfterTheNextInstruction)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0xfb, 0x00, 0x00 });	// EI, NOP, NOP

	Processor.DoNextInstruction();
	EXPECT_FALSE(Processor.InterruptsEnabled());

	Processor.DoNextInstruction();
	EXPECT_EQ(2, Processor.RegisterFile().PC);
	EXPECT_TRUE(Processor.InterruptsEnabled());

	Processor.DoNextInstruction();
	EXPECT_EQ(0x40, Processor.RegisterFile().PC);
	EXPECT_EQ(2, Memory.ReadByte(StackTop - 2));
}

TEST_F(CpuInterruptEnableTests, DiStraightAfterEiKeepsInterruptsDisabled)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0xfb, 0xf3, 0x00 });	// EI, DI, NOP

	for (auto i = 0; i < 3; i++) Processor.DoNextInstruction();

	EXPECT_FALSE(Processor.InterruptsEnabled());
	EXPECT_EQ(3, Processor.RegisterFile().PC);
}

TEST_F(CpuInterruptEnableTests, RetiEnablesInterruptsStraightAway)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0xd9 });					// RETI
	Memory.SetBytes(StackTop, { 0x00, 0x10 });

	Processor.DoNextInstruction();
	EXPECT_TRUE(Processor.InterruptsEnabled());

	Processor.DoNextInstruction();
	EXPECT_EQ(0x40, Processor.RegisterFile().PC);
	EXPECT_EQ(0x00, Memory.ReadByte(StackTop));
	EXPECT_EQ(0x10, Memory.ReadByte(StackTop + 1));
}

TEST_F(CpuInterruptEnableTests, EiWithInterruptsAlreadyEnabledDoesNotReenableThemInTheHandler)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0xfb, 0x00 });			// EI, NOP
	Memory.SetBytes(0x40, { 0x00, 0x00 });							// NOP, NOP
	Processor.WaitingInterrupts() = 0;
	Processor.InterruptsEnabled() = true;

	Processor.DoNextInstruction();
	Processor.RequestInterrupt(InterruptFlags::VBlankInt);

	Processor.DoNextInstruction();
	EXPECT_EQ(0x40, Processor.RegisterFile().PC);

	for (auto i = 0; i < 2; i++)
	{
		Processor.DoNextInstruction();
		EXPECT_FALSE(Processor.InterruptsEnabled());
	}
}

TEST_F(CpuInterruptEnableTests, EiTwiceEnablesInterruptsAfterTheSecondAndNotAgainInTheHandler)
{
	Memory.SetBytes(MemoryMap::RomFixed, { 0xfb, 0xfb, 0x00 });	// EI, EI, NOP
	Memory.SetBytes(0x40, { 0x00, 0x00 });							// NOP, NOP

	Processor.DoNextInstruction();
	EXPECT_FALSE(Processor.InterruptsEnabled());

	// The first EI takes effect after the second
	Processor.DoNextInstruction();
	EXPECT_TRUE(Processor.InterruptsEnabled());

	Processor.DoNextInstruction();
	EXPECT_EQ(0x40, Processor.RegisterFile().PC);
	EXPECT_EQ(2, Memory.ReadByte(StackTop - 2));

	for (auto i = 0; i < 2; i++)
	{
		Processor.DoNextInstruction();
		EXPECT_FALSE(Processor.InterruptsEnabled());
	}
}

class CpuHaltTests : public CpuTimingTests
{
public:
	CpuHaltTests()
	{
		// HALT, INC A, INC A
		Memory.SetBytes(MemoryMap::RomFixed, { 0x76, 0x3c, 0x3c });
		Processor.EnabledInterrupts() = InterruptFlags::TimerInt;
	}
};

TEST_F(CpuHaltTests, HaltedCpuRunsOneMachineCycleAtATime)
{
	Processor.DoNextInstruction();

	for (auto i = 0; i < 10; i++)
	{
		EXPECT_EQ(MachineCycle, Processor.DoNextInstruction());
		EXPECT_EQ(1, Processor.RegisterFile().PC);
	}

	EXPECT_EQ(0, Processor.RegisterFile().A);
}

// Per mooneye-test-suite's halt_ime0_nointr_timing and halt_ime1_timing2-GS, the CPU leaves HALT with the same timing
// as if it had been running NOPs: the halted machine cycle that sees the interrupt stands in for an opcode fetch
TEST_F(CpuHaltTests, InterruptWakesCpuWithoutDispatchWhenImeIsClear)
{
	Processor.DoNextInstruction();
	Processor.DoNextInstruction();
	Processor.RequestInterrupt(InterruptFlags::TimerInt);

	// The halted cycle fetches INC A, which then completes
	EXPECT_EQ(MachineCycle, Processor.DoNextInstruction());
	EXPECT_EQ(2, Processor.RegisterFile().PC);
	EXPECT_EQ(1, Processor.RegisterFile().A);
	EXPECT_EQ(InterruptFlags::TimerInt, Processor.WaitingInterrupts());
}

TEST_F(CpuHaltTests, InterruptWakesCpuAndIsDispatchedWhenImeIsSet)
{
	Processor.InterruptsEnabled() = true;
	Processor.DoNextInstruction();
	Processor.RequestInterrupt(InterruptFlags::TimerInt);

	// The halted cycle, then the dispatch's four other cycles, as when an interrupt arrives during a NOP
	EXPECT_EQ(5 * MachineCycle, Processor.DoNextInstruction());
	EXPECT_EQ(0x50, Processor.RegisterFile().PC);
	EXPECT_EQ(1, Memory.ReadByte(StackTop - 2));
}

TEST_F(CpuHaltTests, InterruptDuringHaltTakesSameTimeAsDuringNops)
{
	Processor.InterruptsEnabled() = true;
	Processor.DoNextInstruction();
	Processor.RequestInterrupt(InterruptFlags::TimerInt);
	auto haltedCycles = Processor.DoNextInstruction();
	EXPECT_EQ(0x50, Processor.RegisterFile().PC);

	// The same interrupt arriving while the CPU runs NOPs
	Memory.SetBytes(0x10, { 0x00, 0x00 });
	Processor.RegisterFile().PC = 0x10;
	Processor.InterruptsEnabled() = true;
	Processor.DoNextInstruction();
	Processor.RequestInterrupt(InterruptFlags::TimerInt);
	auto nopCycles = Processor.DoNextInstruction();
	EXPECT_EQ(0x50, Processor.RegisterFile().PC);

	EXPECT_EQ(nopCycles, haltedCycles);
}

TEST_F(CpuHaltTests, InterruptsNotEnabledInIeDoNotWakeCpu)
{
	Processor.DoNextInstruction();
	Processor.RequestInterrupt(InterruptFlags::VBlankInt);

	EXPECT_EQ(MachineCycle, Processor.DoNextInstruction());
	EXPECT_EQ(1, Processor.RegisterFile().PC);
}

TEST_F(CpuHaltTests, InterruptPendingWithImeSetIsDispatchedBeforeHaltRuns)
{
	// The interrupt is taken as HALT's opcode is fetched, so the handler returns to the HALT
	Processor.InterruptsEnabled() = true;
	Processor.RequestInterrupt(InterruptFlags::TimerInt);
	Memory.SetBytes(0x50, { 0xd9 });									// RETI

	EXPECT_EQ(5 * MachineCycle, Processor.DoNextInstruction());
	EXPECT_EQ(0x50, Processor.RegisterFile().PC);
	EXPECT_EQ(0, Memory.ReadByte(StackTop - 2));

	// After RETI the HALT runs and, with nothing pending, halts
	Processor.DoNextInstruction();
	Processor.DoNextInstruction();
	EXPECT_EQ(1, Processor.RegisterFile().PC);
	EXPECT_EQ(MachineCycle, Processor.DoNextInstruction());
	EXPECT_EQ(1, Processor.RegisterFile().PC);
}

TEST_F(CpuHaltTests, HaltBugReadsByteAfterHaltTwice)
{
	// IME clear with an interrupt already pending: the CPU doesn't halt, and INC A runs twice
	Processor.RequestInterrupt(InterruptFlags::TimerInt);

	Processor.DoNextInstruction();
	Processor.DoNextInstruction();
	EXPECT_EQ(1, Processor.RegisterFile().PC);
	EXPECT_EQ(1, Processor.RegisterFile().A);

	Processor.DoNextInstruction();
	EXPECT_EQ(2, Processor.RegisterFile().PC);
	EXPECT_EQ(2, Processor.RegisterFile().A);
}

TEST_F(CpuHaltTests, HaltBugAfterEiReturnsFromInterruptToHalt)
{
	// EI, HALT with an interrupt pending: IME is still clear when HALT runs, so the HALT bug strikes, then the
	// interrupt is dispatched with the HALT's address as the return address
	Memory.SetBytes(MemoryMap::RomFixed, { 0xfb, 0x76, 0x3c });
	Processor.RequestInterrupt(InterruptFlags::TimerInt);

	Processor.DoNextInstruction();
	Processor.DoNextInstruction();
	Processor.DoNextInstruction();

	EXPECT_EQ(0x50, Processor.RegisterFile().PC);
	EXPECT_EQ(1, Memory.ReadByte(StackTop - 2));
	EXPECT_EQ(0, Memory.ReadByte(StackTop - 1));
}
