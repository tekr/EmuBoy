#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"

// Machine-cycle timing of memory accesses, interrupt dispatch, EI and HALT. Expectations come from
// https://gbdev.io/gb-opcodes/optables/, https://gbdev.io/pandocs/Interrupts.html and https://gbdev.io/pandocs/halt.html

namespace
{
	const int MachineCycle = 4;

	const unsigned short CounterAddress = 0xff80;
	const unsigned short StackTop = 0xc010;

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
			_memoryMap.WriteByte(CounterAddress, static_cast<unsigned char>(_memoryMap.ReadByte(CounterAddress) + cycles / MachineCycle));
		}
	};
}

class CpuTimingTests : public CpuTestFixture
{
public:
	CycleCountingHardware Hardware{ MemoryMap };

	CpuTimingTests()
	{
		Cpu.SetClockedHardware(&Hardware);
		Cpu.Registers().PC = 0;
		Cpu.Registers().SP = StackTop;
		Cpu.InterruptsEnabled() = false;
		Cpu.EnabledInterrupts() = 0;
		Cpu.WaitingInterrupts() = 0;
	}

	void RunProgram(std::vector<unsigned char>&& program, unsigned short address = MemoryMap::RomFixed)
	{
		MemoryMap.SetBytes(address, std::move(program));
		Cpu.Registers().PC = address;
		MemoryMap.WriteByte(CounterAddress, 0);
		Cycles = Cpu.DoNextInstruction();
	}

	unsigned char Counter() { return MemoryMap.ReadByte(CounterAddress); }

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
		Cpu.Registers().HL = CounterAddress;
		Cpu.Registers().C = CounterAddress & 0xff;
		Cpu.Registers().PC = 0;

		auto program = read.first;
		RunProgram(std::move(program));

		EXPECT_EQ(read.second, Cpu.Registers().A);
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
		Cpu.Registers().HL = CounterAddress;
		Cpu.Registers().C = CounterAddress & 0xff;
		Cpu.Registers().A = 0x40;
		Cpu.Registers().PC = 0;

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
	Cpu.Registers().HL = CounterAddress;
	RunProgram({ 0x35 });
	EXPECT_EQ(1, Counter());
	EXPECT_EQ(3 * MachineCycle, Cycles);

	// SWAP (HL): reads 3 in cycle 3, writes 0x30 in cycle 4
	Cpu.Registers().PC = 0;
	RunProgram({ 0xcb, 0x36 });
	EXPECT_EQ(0x30, Counter());
	EXPECT_EQ(4 * MachineCycle, Cycles);
}

TEST_F(CpuTimingTests, BitTestOfMemoryReadsInTheLastCycle)
{
	// BIT 0,(HL) and BIT 1,(HL) read 3 (binary 11) in cycle 3; in cycle 2 bit 0 would be clear
	Cpu.Registers().HL = CounterAddress;
	RunProgram({ 0xcb, 0x46 });
	EXPECT_EQ(0, Cpu.Registers().F & ZeroFlag);
	EXPECT_EQ(3 * MachineCycle, Cycles);
}

// Stack accesses: pointing SP at the counter shows which cycle each byte is pushed or popped in

TEST_F(CpuTimingTests, PushWritesHighByteInCycleThreeAndLowByteInCycleFour)
{
	Cpu.Registers().BC = 0x1234;

	// The high byte lands on the counter with one cycle still to run
	Cpu.Registers().SP = CounterAddress + 1;
	RunProgram({ 0xc5 });
	EXPECT_EQ(0x13, Counter());
	EXPECT_EQ(4 * MachineCycle, Cycles);

	Cpu.Registers().SP = CounterAddress + 2;
	RunProgram({ 0xc5 });
	EXPECT_EQ(0x34, Counter());
}

TEST_F(CpuTimingTests, RstPushesInCyclesThreeAndFour)
{
	// Returns to 0x0231
	Cpu.Registers().SP = CounterAddress + 1;
	RunProgram({ 0xcf }, 0x0230);
	EXPECT_EQ(0x03, Counter());
	EXPECT_EQ(4 * MachineCycle, Cycles);
	EXPECT_EQ(0x08, Cpu.Registers().PC);

	Cpu.Registers().SP = CounterAddress + 2;
	RunProgram({ 0xcf }, 0x0230);
	EXPECT_EQ(0x31, Counter());
}

TEST_F(CpuTimingTests, CallPushesInCyclesFiveAndSix)
{
	// CALL and CALL NZ (taken, as Z is clear) return to 0x0233
	for (unsigned char opcode : { 0xcd, 0xc4 })
	{
		SCOPED_TRACE(static_cast<int>(opcode));
		Cpu.Registers().F = NoFlags;

		Cpu.Registers().SP = CounterAddress + 1;
		RunProgram({ opcode, 0x00, 0x10 }, 0x0230);
		EXPECT_EQ(0x03, Counter());
		EXPECT_EQ(6 * MachineCycle, Cycles);
		EXPECT_EQ(0x1000, Cpu.Registers().PC);

		Cpu.Registers().SP = CounterAddress + 2;
		RunProgram({ opcode, 0x00, 0x10 }, 0x0230);
		EXPECT_EQ(0x33, Counter());
	}

	// CALL NZ not taken
	Cpu.Registers().F = ZeroFlag;
	Cpu.Registers().SP = CounterAddress + 2;
	RunProgram({ 0xc4, 0x00, 0x10 }, 0x0230);
	EXPECT_EQ(3 * MachineCycle, Cycles);
	EXPECT_EQ(0x0233, Cpu.Registers().PC);
}

TEST_F(CpuTimingTests, ReturnPopsLowByteAfterAnyConditionCheck)
{
	MemoryMap.WriteByte(CounterAddress + 1, 0x12);

	// RET: low byte popped in cycle 2
	Cpu.Registers().SP = CounterAddress;
	RunProgram({ 0xc9 });
	EXPECT_EQ(0x1202, Cpu.Registers().PC);
	EXPECT_EQ(4 * MachineCycle, Cycles);

	// RET NZ taken: a cycle to check the condition first
	Cpu.Registers().F = NoFlags;
	Cpu.Registers().SP = CounterAddress;
	RunProgram({ 0xc0 });
	EXPECT_EQ(0x1203, Cpu.Registers().PC);
	EXPECT_EQ(5 * MachineCycle, Cycles);

	// RET NZ not taken
	Cpu.Registers().F = ZeroFlag;
	Cpu.Registers().SP = CounterAddress;
	RunProgram({ 0xc0 });
	EXPECT_EQ(1, Cpu.Registers().PC);
	EXPECT_EQ(2 * MachineCycle, Cycles);
}

TEST_F(CpuTimingTests, PopReadsLowByteInCycleTwo)
{
	MemoryMap.WriteByte(CounterAddress + 1, 0x12);
	Cpu.Registers().SP = CounterAddress;
	RunProgram({ 0xc1 });

	EXPECT_EQ(0x1202, Cpu.Registers().BC);
	EXPECT_EQ(3 * MachineCycle, Cycles);
}

TEST_F(CpuTimingTests, StoreSpWritesLowByteInCycleFourAndHighByteInCycleFive)
{
	Cpu.Registers().SP = 0xabcd;
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
	EXPECT_EQ(StackTop + 1, Cpu.Registers().SP);

	RunProgram({ 0xf8, 0x01 });
	EXPECT_EQ(3 * MachineCycle, Cycles);
	EXPECT_EQ(StackTop + 2, Cpu.Registers().HL);
}

TEST_F(CpuTimingTests, InterruptDispatchTakesFiveMachineCycles)
{
	Cpu.InterruptsEnabled() = true;
	Cpu.EnabledInterrupts() = InterruptFlags::TimerInt;
	Cpu.RequestInterrupt(InterruptFlags::TimerInt);
	Cpu.Registers().PC = 0x1234;

	EXPECT_EQ(5 * MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(0x50, Cpu.Registers().PC);
	EXPECT_EQ(0, Cpu.WaitingInterrupts());
	EXPECT_FALSE(Cpu.InterruptsEnabled());
	EXPECT_EQ(0x34, MemoryMap.ReadByte(StackTop - 2));
	EXPECT_EQ(0x12, MemoryMap.ReadByte(StackTop - 1));
}

TEST_F(CpuTimingTests, HighestPriorityInterruptIsDispatchedFirst)
{
	Cpu.InterruptsEnabled() = true;
	Cpu.EnabledInterrupts() = InterruptFlags::AllInt;
	Cpu.RequestInterrupt(static_cast<InterruptFlags>(InterruptFlags::SerialInt | InterruptFlags::LcdStatInt));

	Cpu.DoNextInstruction();
	EXPECT_EQ(0x48, Cpu.Registers().PC);
	EXPECT_EQ(InterruptFlags::SerialInt, Cpu.WaitingInterrupts());
}

TEST_F(CpuTimingTests, InterruptIsChosenAfterPushingHighByteOfPC)
{
	// With SP at 0, the high byte of PC is pushed to IE at 0xffff
	Cpu.InterruptsEnabled() = true;
	Cpu.Registers().SP = 0;

	// The push leaves only the timer interrupt enabled
	Cpu.Registers().PC = InterruptFlags::TimerInt << 8;
	Cpu.EnabledInterrupts() = InterruptFlags::VBlankInt;
	Cpu.RequestInterrupt(static_cast<InterruptFlags>(InterruptFlags::VBlankInt | InterruptFlags::TimerInt));
	Cpu.DoNextInstruction();
	EXPECT_EQ(0x50, Cpu.Registers().PC);

	// The push disables the only pending interrupt, so the CPU jumps to 0 and the interrupt stays pending
	Cpu.InterruptsEnabled() = true;
	Cpu.Registers().SP = 0;
	Cpu.Registers().PC = 0x0200;
	Cpu.EnabledInterrupts() = InterruptFlags::VBlankInt;
	Cpu.WaitingInterrupts() = InterruptFlags::VBlankInt;
	Cpu.DoNextInstruction();
	EXPECT_EQ(0, Cpu.Registers().PC);
	EXPECT_EQ(InterruptFlags::VBlankInt, Cpu.WaitingInterrupts());
}

class CpuInterruptEnableTests : public CpuTimingTests
{
public:
	CpuInterruptEnableTests()
	{
		Cpu.EnabledInterrupts() = InterruptFlags::VBlankInt;
		Cpu.RequestInterrupt(InterruptFlags::VBlankInt);
	}
};

TEST_F(CpuInterruptEnableTests, EiTakesEffectAfterTheNextInstruction)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfb, 0x00, 0x00 });	// EI, NOP, NOP

	Cpu.DoNextInstruction();
	EXPECT_FALSE(Cpu.InterruptsEnabled());

	Cpu.DoNextInstruction();
	EXPECT_EQ(2, Cpu.Registers().PC);
	EXPECT_TRUE(Cpu.InterruptsEnabled());

	Cpu.DoNextInstruction();
	EXPECT_EQ(0x40, Cpu.Registers().PC);
	EXPECT_EQ(2, MemoryMap.ReadByte(StackTop - 2));
}

TEST_F(CpuInterruptEnableTests, DiStraightAfterEiKeepsInterruptsDisabled)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfb, 0xf3, 0x00 });	// EI, DI, NOP

	for (auto i = 0; i < 3; i++) Cpu.DoNextInstruction();

	EXPECT_FALSE(Cpu.InterruptsEnabled());
	EXPECT_EQ(3, Cpu.Registers().PC);
}

TEST_F(CpuInterruptEnableTests, RetiEnablesInterruptsStraightAway)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xd9 });					// RETI
	MemoryMap.SetBytes(StackTop, { 0x00, 0x10 });

	Cpu.DoNextInstruction();
	EXPECT_TRUE(Cpu.InterruptsEnabled());

	Cpu.DoNextInstruction();
	EXPECT_EQ(0x40, Cpu.Registers().PC);
	EXPECT_EQ(0x00, MemoryMap.ReadByte(StackTop));
	EXPECT_EQ(0x10, MemoryMap.ReadByte(StackTop + 1));
}

TEST_F(CpuInterruptEnableTests, EiWithInterruptsAlreadyEnabledDoesNotReenableThemInTheHandler)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfb, 0x00 });			// EI, NOP
	MemoryMap.SetBytes(0x40, { 0x00, 0x00 });							// NOP, NOP
	Cpu.WaitingInterrupts() = 0;
	Cpu.InterruptsEnabled() = true;

	Cpu.DoNextInstruction();
	Cpu.RequestInterrupt(InterruptFlags::VBlankInt);

	Cpu.DoNextInstruction();
	EXPECT_EQ(0x40, Cpu.Registers().PC);

	for (auto i = 0; i < 2; i++)
	{
		Cpu.DoNextInstruction();
		EXPECT_FALSE(Cpu.InterruptsEnabled());
	}
}

TEST_F(CpuInterruptEnableTests, EiTwiceEnablesInterruptsAfterTheSecondAndNotAgainInTheHandler)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfb, 0xfb, 0x00 });	// EI, EI, NOP
	MemoryMap.SetBytes(0x40, { 0x00, 0x00 });							// NOP, NOP

	Cpu.DoNextInstruction();
	EXPECT_FALSE(Cpu.InterruptsEnabled());

	// The first EI takes effect after the second
	Cpu.DoNextInstruction();
	EXPECT_TRUE(Cpu.InterruptsEnabled());

	Cpu.DoNextInstruction();
	EXPECT_EQ(0x40, Cpu.Registers().PC);
	EXPECT_EQ(2, MemoryMap.ReadByte(StackTop - 2));

	for (auto i = 0; i < 2; i++)
	{
		Cpu.DoNextInstruction();
		EXPECT_FALSE(Cpu.InterruptsEnabled());
	}
}

class CpuHaltTests : public CpuTimingTests
{
public:
	CpuHaltTests()
	{
		// HALT, INC A, INC A
		MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x76, 0x3c, 0x3c });
		Cpu.EnabledInterrupts() = InterruptFlags::TimerInt;
	}
};

TEST_F(CpuHaltTests, HaltedCpuRunsOneMachineCycleAtATime)
{
	Cpu.DoNextInstruction();

	for (auto i = 0; i < 10; i++)
	{
		EXPECT_EQ(MachineCycle, Cpu.DoNextInstruction());
		EXPECT_EQ(1, Cpu.Registers().PC);
	}

	EXPECT_EQ(0, Cpu.Registers().A);
}

TEST_F(CpuHaltTests, InterruptWakesCpuWithoutDispatchWhenImeIsClear)
{
	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	Cpu.RequestInterrupt(InterruptFlags::TimerInt);

	// A machine cycle to wake up, then INC A
	EXPECT_EQ(2 * MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(2, Cpu.Registers().PC);
	EXPECT_EQ(1, Cpu.Registers().A);
	EXPECT_EQ(InterruptFlags::TimerInt, Cpu.WaitingInterrupts());
}

TEST_F(CpuHaltTests, InterruptWakesCpuAndIsDispatchedWhenImeIsSet)
{
	Cpu.InterruptsEnabled() = true;
	Cpu.DoNextInstruction();
	Cpu.RequestInterrupt(InterruptFlags::TimerInt);

	// A machine cycle to wake up, then five to dispatch
	EXPECT_EQ(6 * MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(0x50, Cpu.Registers().PC);
	EXPECT_EQ(1, MemoryMap.ReadByte(StackTop - 2));
}

TEST_F(CpuHaltTests, InterruptsNotEnabledInIeDoNotWakeCpu)
{
	Cpu.DoNextInstruction();
	Cpu.RequestInterrupt(InterruptFlags::VBlankInt);

	EXPECT_EQ(MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(1, Cpu.Registers().PC);
}

TEST_F(CpuHaltTests, InterruptPendingWithImeSetIsDispatchedBeforeHaltRuns)
{
	// The interrupt is taken as HALT's opcode is fetched, so the handler returns to the HALT
	Cpu.InterruptsEnabled() = true;
	Cpu.RequestInterrupt(InterruptFlags::TimerInt);
	MemoryMap.SetBytes(0x50, { 0xd9 });									// RETI

	EXPECT_EQ(5 * MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(0x50, Cpu.Registers().PC);
	EXPECT_EQ(0, MemoryMap.ReadByte(StackTop - 2));

	// After RETI the HALT runs and, with nothing pending, halts
	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	EXPECT_EQ(1, Cpu.Registers().PC);
	EXPECT_EQ(MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(1, Cpu.Registers().PC);
}

TEST_F(CpuHaltTests, HaltBugReadsByteAfterHaltTwice)
{
	// IME clear with an interrupt already pending: the CPU doesn't halt, and INC A runs twice
	Cpu.RequestInterrupt(InterruptFlags::TimerInt);

	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	EXPECT_EQ(1, Cpu.Registers().PC);
	EXPECT_EQ(1, Cpu.Registers().A);

	Cpu.DoNextInstruction();
	EXPECT_EQ(2, Cpu.Registers().PC);
	EXPECT_EQ(2, Cpu.Registers().A);
}

TEST_F(CpuHaltTests, HaltBugAfterEiReturnsFromInterruptToHalt)
{
	// EI, HALT with an interrupt pending: IME is still clear when HALT runs, so the HALT bug strikes, then the
	// interrupt is dispatched with the HALT's address as the return address
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfb, 0x76, 0x3c });
	Cpu.RequestInterrupt(InterruptFlags::TimerInt);

	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();

	EXPECT_EQ(0x50, Cpu.Registers().PC);
	EXPECT_EQ(1, MemoryMap.ReadByte(StackTop - 2));
	EXPECT_EQ(0, MemoryMap.ReadByte(StackTop - 1));
}
