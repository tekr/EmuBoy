#include "stdafx.h"
#include <gtest/gtest.h>
#include "CpuTestFixture.h"
#include "../core/Timer.h"

// Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html, the counter (TIMA) increments on the falling
// edge of divider bit 9, 3, 5 or 7 (for modes 0-3) ANDed with the timer enable bit

const int DivReg = 0;
const int CounterReg = 1;
const int ModuloReg = 2;
const int ControlReg = 3;

const int TimerEnable = 4;

// Cycles per counter increment for each mode
const int ModePeriods[] = { 1024, 16, 64, 256 };

class TimerQuirkTests : public CpuTestFixture
{
public:
	Timer Timer;

	TimerQuirkTests()
	{
		Timer.SetCpu(&Cpu);
		Cpu.WaitingInterrupts() = 0;
	}

	unsigned char Counter() { return Timer.ReadRegister(CounterReg); }
};

TEST_F(TimerQuirkTests, CountsAtTheRateOfEachMode)
{
	for (auto mode = 0; mode < 4; mode++)
	{
		SCOPED_TRACE(mode);
		Timer.WriteRegister(DivReg, 0);
		Timer.WriteRegister(ControlReg, TimerEnable | mode);
		Timer.WriteRegister(CounterReg, 0);

		Timer.RunCycles(ModePeriods[mode] - 1);
		EXPECT_EQ(0, Counter());

		Timer.RunCycles(1);
		EXPECT_EQ(1, Counter());

		Timer.RunCycles(ModePeriods[mode] * 10);
		EXPECT_EQ(11, Counter());

		Timer.WriteRegister(ControlReg, mode);
	}
}

TEST_F(TimerQuirkTests, DivIsTheUpperByteOfTheDivider)
{
	Timer.RunCycles(0x1234);
	EXPECT_EQ(0x12, Timer.ReadRegister(DivReg));

	Timer.WriteRegister(DivReg, 0x55);
	EXPECT_EQ(0, Timer.ReadRegister(DivReg));
}

TEST_F(TimerQuirkTests, DivWriteInSecondHalfOfPeriodIncrementsCounter)
{
	Timer.WriteRegister(ControlReg, TimerEnable | 0);
	Timer.RunCycles(768);

	Timer.WriteRegister(DivReg, 0);
	EXPECT_EQ(1, Counter());
}

TEST_F(TimerQuirkTests, DivWriteInFirstHalfOfPeriodDoesNotIncrementCounter)
{
	Timer.WriteRegister(ControlReg, TimerEnable | 0);
	Timer.RunCycles(256);

	Timer.WriteRegister(DivReg, 0);
	EXPECT_EQ(0, Counter());
}

TEST_F(TimerQuirkTests, DivWriteRestartsThePeriod)
{
	for (auto cyclesBeforeWrite : { 4, 12 })
	{
		SCOPED_TRACE(cyclesBeforeWrite);
		Timer.WriteRegister(ControlReg, 0);
		Timer.WriteRegister(DivReg, 0);
		Timer.WriteRegister(ControlReg, TimerEnable | 1);
		Timer.WriteRegister(CounterReg, 0);

		Timer.RunCycles(cyclesBeforeWrite);
		Timer.WriteRegister(DivReg, 0);
		auto counter = Counter();

		Timer.RunCycles(15);
		EXPECT_EQ(counter, Counter());

		Timer.RunCycles(1);
		EXPECT_EQ(counter + 1, Counter());
	}
}

TEST_F(TimerQuirkTests, DisablingTimerWhileSelectedBitIsSetIncrementsCounter)
{
	Timer.WriteRegister(ControlReg, TimerEnable | 1);
	Timer.RunCycles(12);

	Timer.WriteRegister(ControlReg, 1);
	EXPECT_EQ(1, Counter());
}

TEST_F(TimerQuirkTests, DisablingTimerWhileSelectedBitIsClearDoesNotIncrementCounter)
{
	Timer.WriteRegister(ControlReg, TimerEnable | 1);
	Timer.RunCycles(4);

	Timer.WriteRegister(ControlReg, 1);
	EXPECT_EQ(0, Counter());
}

TEST_F(TimerQuirkTests, ModeChangeFromSetBitToClearBitIncrementsCounter)
{
	// Divider 8: bit 3 (mode 1) is set, bit 5 (mode 2) is clear
	Timer.WriteRegister(ControlReg, TimerEnable | 1);
	Timer.RunCycles(8);

	Timer.WriteRegister(ControlReg, TimerEnable | 2);
	EXPECT_EQ(1, Counter());
}

TEST_F(TimerQuirkTests, ModeChangeFromClearBitToSetBitDoesNotIncrementCounter)
{
	// Divider 32: bit 3 (mode 1) is clear, bit 5 (mode 2) is set
	Timer.WriteRegister(ControlReg, TimerEnable | 1);
	Timer.RunCycles(32);
	Timer.WriteRegister(CounterReg, 0);

	Timer.WriteRegister(ControlReg, TimerEnable | 2);
	EXPECT_EQ(0, Counter());
}

TEST_F(TimerQuirkTests, OverflowReloadsModuloAndRequestsInterrupt)
{
	Timer.WriteRegister(ModuloReg, 0xf0);
	Timer.WriteRegister(CounterReg, 0xff);
	Timer.WriteRegister(ControlReg, TimerEnable | 1);

	Timer.RunCycles(15);
	EXPECT_EQ(0, Cpu.WaitingInterrupts() & InterruptFlags::TimerInt);

	Timer.RunCycles(1);
	EXPECT_EQ(0xf0, Counter());
	EXPECT_NE(0, Cpu.WaitingInterrupts() & InterruptFlags::TimerInt);
}

TEST_F(TimerQuirkTests, NextEventIsTheSoonerOfTheNextDivAndCounterIncrements)
{
	Timer.RunCycles(4);
	EXPECT_EQ(252, Timer.GetCyclesToNextEvent());

	Timer.WriteRegister(ControlReg, TimerEnable | 1);
	EXPECT_EQ(12, Timer.GetCyclesToNextEvent());

	Timer.WriteRegister(ControlReg, TimerEnable | 0);
	EXPECT_EQ(252, Timer.GetCyclesToNextEvent());
}
