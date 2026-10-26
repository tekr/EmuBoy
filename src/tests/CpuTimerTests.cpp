#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"
#include "../core/Timer.h"

// Runs the CPU with the timer as its clocked hardware, so timer register accesses happen at the machine cycle the
// instruction makes them. Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html, TIMA increments on the
// falling edge of the selected divider bit, and after overflowing reads 0 for a machine cycle (cycle A) before
// being reloaded from TMA and requesting the interrupt (cycle B).

namespace
{
	const unsigned short DivAddress = 0xff04;
	const unsigned short TimaAddress = 0xff05;
	const unsigned short TmaAddress = 0xff06;
	const unsigned short TacAddress = 0xff07;

	// Enabled, 16 cycles per increment (divider bit 3)
	const unsigned char TacMode1 = 0x05;

	const unsigned short StackTop = 0xc010;

	class TimerHardware : public ClockedHardware
	{
		Timer& _timer;

	public:
		explicit TimerHardware(Timer& timer) : _timer(timer) {}

		void RunCycles(int cycles) override { _timer.RunCycles(cycles); }
	};
}

class CpuTimerTests : public CpuTestFixture
{
public:
	Timer Timer;
	TimerHardware Hardware{ Timer };

	CpuTimerTests()
	{
		Timer.SetCpu(&Cpu);
		MemoryMap.SetTimer(&Timer);
		Cpu.SetClockedHardware(&Hardware);

		Cpu.Registers().PC = 0;
		Cpu.Registers().SP = StackTop;
		Cpu.InterruptsEnabled() = false;
		Cpu.EnabledInterrupts() = InterruptFlags::TimerInt;
		Cpu.WaitingInterrupts() = 0;

		// Divider at 0, so TIMA next increments (and overflows) at the end of the 4th machine cycle
		MemoryMap.WriteByte(DivAddress, 0);
		MemoryMap.WriteByte(TmaAddress, 0xf0);
		MemoryMap.WriteByte(TimaAddress, 0xff);
		MemoryMap.WriteByte(TacAddress, TacMode1);
	}

	void Step(int instructions)
	{
		for (auto i = 0; i < instructions; i++) Cpu.DoNextInstruction();
	}

	unsigned char Tima() { return MemoryMap.ReadByte(TimaAddress); }
	bool TimerInterruptRequested() { return (Cpu.WaitingInterrupts() & InterruptFlags::TimerInt) != 0; }
};

TEST_F(CpuTimerTests, InterruptRequestedDuringOpcodeFetchIsDispatchedInsteadOfTheInstruction)
{
	// Disabling the timer in the third cycle of LDH, with divider bit 3 set (divider 12), overflows TIMA. The
	// interrupt is requested a cycle later, during the next opcode fetch, which still sees it
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xe0, 0x07, 0x3c });	// LDH (TAC),A; INC A
	Cpu.Registers().A = 0x01;
	Cpu.InterruptsEnabled() = true;

	Step(1);
	EXPECT_EQ(0, Tima());
	EXPECT_FALSE(TimerInterruptRequested());

	Step(1);
	EXPECT_EQ(0x50, Cpu.Registers().PC);
	EXPECT_EQ(0x01, Cpu.Registers().A);
	EXPECT_EQ(0x02, MemoryMap.ReadByte(StackTop - 2));
	EXPECT_EQ(0xf0, Tima());
}
