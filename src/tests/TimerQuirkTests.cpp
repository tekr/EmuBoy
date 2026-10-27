#include "stdafx.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "CpuTestFixture.h"
#include "../core/Timer.h"
#include "../core/Emulator.h"

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
	Timer TimerUnit;

	TimerQuirkTests()
	{
		TimerUnit.SetCpu(&Processor);
		Processor.WaitingInterrupts() = 0;

		// Starts from a divider of 0 rather than its power-on phase
		TimerUnit.WriteRegister(DivReg, 0);
	}

	unsigned char Counter() { return TimerUnit.ReadRegister(CounterReg); }
};

TEST_F(TimerQuirkTests, CountsAtTheRateOfEachMode)
{
	for (auto mode = 0; mode < 4; mode++)
	{
		SCOPED_TRACE(mode);
		TimerUnit.WriteRegister(DivReg, 0);
		TimerUnit.WriteRegister(ControlReg, TimerEnable | mode);
		TimerUnit.WriteRegister(CounterReg, 0);

		TimerUnit.RunCycles(ModePeriods[mode] - 1);
		EXPECT_EQ(0, Counter());

		TimerUnit.RunCycles(1);
		EXPECT_EQ(1, Counter());

		TimerUnit.RunCycles(ModePeriods[mode] * 10);
		EXPECT_EQ(11, Counter());

		TimerUnit.WriteRegister(ControlReg, mode);
	}
}

TEST(TimerPowerOnTest, DividerHasCountedAMachineCycle)
{
	// Mooneye-test-suite's boot_div-dmgABCmgb: with the divider a machine cycle in at power-on, DIV has the phase
	// the DMG boot ROM hands over with
	Timer timer;

	timer.RunCycles(256 - 4 - 1);
	EXPECT_EQ(0, timer.ReadRegister(DivReg));

	timer.RunCycles(1);
	EXPECT_EQ(1, timer.ReadRegister(DivReg));
}

TEST(TimerPowerOnTest, UnusedControlBitsReadAsOne)
{
	// Mooneye-test-suite's unused_hwio-GS
	Timer timer;
	EXPECT_EQ(0xf8, timer.ReadRegister(ControlReg));

	timer.WriteRegister(ControlReg, 0x05);
	EXPECT_EQ(0xfd, timer.ReadRegister(ControlReg));
}

TEST_F(TimerQuirkTests, DivIsTheUpperByteOfTheDivider)
{
	TimerUnit.RunCycles(0x1234);
	EXPECT_EQ(0x12, TimerUnit.ReadRegister(DivReg));

	TimerUnit.WriteRegister(DivReg, 0x55);
	EXPECT_EQ(0, TimerUnit.ReadRegister(DivReg));
}

TEST_F(TimerQuirkTests, DivWriteInSecondHalfOfPeriodIncrementsCounter)
{
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 0);
	TimerUnit.RunCycles(768);

	TimerUnit.WriteRegister(DivReg, 0);
	EXPECT_EQ(1, Counter());
}

TEST_F(TimerQuirkTests, DivWriteInFirstHalfOfPeriodDoesNotIncrementCounter)
{
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 0);
	TimerUnit.RunCycles(256);

	TimerUnit.WriteRegister(DivReg, 0);
	EXPECT_EQ(0, Counter());
}

TEST_F(TimerQuirkTests, DivWriteRestartsThePeriod)
{
	for (auto cyclesBeforeWrite : { 4, 12 })
	{
		SCOPED_TRACE(cyclesBeforeWrite);
		TimerUnit.WriteRegister(ControlReg, 0);
		TimerUnit.WriteRegister(DivReg, 0);
		TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
		TimerUnit.WriteRegister(CounterReg, 0);

		TimerUnit.RunCycles(cyclesBeforeWrite);
		TimerUnit.WriteRegister(DivReg, 0);
		auto counter = Counter();

		TimerUnit.RunCycles(15);
		EXPECT_EQ(counter, Counter());

		TimerUnit.RunCycles(1);
		EXPECT_EQ(counter + 1, Counter());
	}
}

TEST_F(TimerQuirkTests, DisablingTimerWhileSelectedBitIsSetIncrementsCounter)
{
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
	TimerUnit.RunCycles(12);

	TimerUnit.WriteRegister(ControlReg, 1);
	EXPECT_EQ(1, Counter());
}

TEST_F(TimerQuirkTests, DisablingTimerWhileSelectedBitIsClearDoesNotIncrementCounter)
{
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
	TimerUnit.RunCycles(4);

	TimerUnit.WriteRegister(ControlReg, 1);
	EXPECT_EQ(0, Counter());
}

TEST_F(TimerQuirkTests, ModeChangeFromSetBitToClearBitIncrementsCounter)
{
	// Divider 8: bit 3 (mode 1) is set, bit 5 (mode 2) is clear
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
	TimerUnit.RunCycles(8);

	TimerUnit.WriteRegister(ControlReg, TimerEnable | 2);
	EXPECT_EQ(1, Counter());
}

TEST_F(TimerQuirkTests, ModeChangeFromClearBitToSetBitDoesNotIncrementCounter)
{
	// Divider 32: bit 3 (mode 1) is clear, bit 5 (mode 2) is set
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
	TimerUnit.RunCycles(32);
	TimerUnit.WriteRegister(CounterReg, 0);

	TimerUnit.WriteRegister(ControlReg, TimerEnable | 2);
	EXPECT_EQ(0, Counter());
}

// Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html#timer-overflow-behavior, after TIMA overflows it
// reads 0 for one machine cycle (cycle A). Then it's reloaded from TMA and the interrupt is requested (cycle B).
// A TIMA write in cycle A cancels the reload; in cycle B it's ignored. A TMA write in cycle B also goes to TIMA.

class TimerOverflowTests : public TimerQuirkTests
{
public:
	TimerOverflowTests()
	{
		TimerUnit.WriteRegister(ModuloReg, 0xf0);
		TimerUnit.WriteRegister(CounterReg, 0xff);
		TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
	}

	bool TimerInterruptRequested() { return (Processor.WaitingInterrupts() & InterruptFlags::TimerInt) != 0; }

	// Runs to the start of cycle A: the counter has just overflowed
	void RunToOverflow() { TimerUnit.RunCycles(16); }
};

TEST_F(TimerOverflowTests, CounterReadsZeroForOneMachineCycleAfterOverflow)
{
	TimerUnit.RunCycles(15);
	EXPECT_EQ(0xff, Counter());

	TimerUnit.RunCycles(1);
	EXPECT_EQ(0, Counter());
	EXPECT_FALSE(TimerInterruptRequested());

	TimerUnit.RunCycles(3);
	EXPECT_EQ(0, Counter());
	EXPECT_FALSE(TimerInterruptRequested());
}

TEST_F(TimerOverflowTests, ReloadsModuloAndRequestsInterruptAMachineCycleAfterOverflow)
{
	RunToOverflow();
	TimerUnit.RunCycles(4);

	EXPECT_EQ(0xf0, Counter());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(TimerOverflowTests, ReloadsWhenRunInOneLargeStep)
{
	TimerUnit.RunCycles(16 * 3);

	EXPECT_EQ(0xf2, Counter());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(TimerOverflowTests, CounterWriteBeforeReloadCancelsIt)
{
	RunToOverflow();
	TimerUnit.WriteRegister(CounterReg, 0x42);
	TimerUnit.RunCycles(4);

	EXPECT_EQ(0x42, Counter());
	EXPECT_FALSE(TimerInterruptRequested());
}

TEST_F(TimerOverflowTests, CounterWriteDuringReloadCycleIsIgnored)
{
	RunToOverflow();
	TimerUnit.RunCycles(4);
	TimerUnit.WriteRegister(CounterReg, 0x42);

	EXPECT_EQ(0xf0, Counter());
}

TEST_F(TimerOverflowTests, CounterWriteAfterReloadCycleTakesEffect)
{
	RunToOverflow();
	TimerUnit.RunCycles(8);
	TimerUnit.WriteRegister(CounterReg, 0x42);

	EXPECT_EQ(0x42, Counter());
}

TEST_F(TimerOverflowTests, ModuloWriteBeforeReloadIsReloaded)
{
	RunToOverflow();
	TimerUnit.WriteRegister(ModuloReg, 0x37);
	TimerUnit.RunCycles(4);

	EXPECT_EQ(0x37, Counter());
}

TEST_F(TimerOverflowTests, ModuloWriteDuringReloadCycleAlsoLoadsCounter)
{
	RunToOverflow();
	TimerUnit.RunCycles(4);
	TimerUnit.WriteRegister(ModuloReg, 0x37);

	EXPECT_EQ(0x37, Counter());
	EXPECT_EQ(0x37, TimerUnit.ReadRegister(ModuloReg));
}

TEST_F(TimerOverflowTests, ModuloWriteAfterReloadCycleDoesNotLoadCounter)
{
	RunToOverflow();
	TimerUnit.RunCycles(8);
	TimerUnit.WriteRegister(ModuloReg, 0x37);

	EXPECT_EQ(0xf0, Counter());
}

TEST_F(TimerOverflowTests, OverflowCausedByTacWriteHasTheSameReloadCycles)
{
	// Disabling the timer with divider bit 3 set increments TIMA, overflowing it
	TimerUnit.RunCycles(12);
	TimerUnit.WriteRegister(ControlReg, 1);
	EXPECT_EQ(0, Counter());

	TimerUnit.WriteRegister(CounterReg, 0x42);
	TimerUnit.RunCycles(4);

	EXPECT_EQ(0x42, Counter());
	EXPECT_FALSE(TimerInterruptRequested());
}

TEST_F(TimerOverflowTests, OverflowCausedByTacWriteReloadsEvenThoughTimerIsDisabled)
{
	TimerUnit.RunCycles(12);
	TimerUnit.WriteRegister(ControlReg, 1);
	TimerUnit.RunCycles(4);

	EXPECT_EQ(0xf0, Counter());
	EXPECT_TRUE(TimerInterruptRequested());
}

TEST_F(TimerQuirkTests, CountsAcrossDividerWraparound)
{
	TimerUnit.WriteRegister(ControlReg, TimerEnable | 1);
	TimerUnit.RunCycles(0xfff8);
	TimerUnit.WriteRegister(CounterReg, 0);

	TimerUnit.RunCycles(16);
	EXPECT_EQ(1, Counter());
	EXPECT_EQ(0, TimerUnit.ReadRegister(DivReg));
}

// Runs a program from address 0 through the whole emulator, so the timer must see
// register writes at the cycle they happen
class TimerEmulatorTests : public testing::Test
{
public:
	std::shared_ptr<TestCartridge> Rom{ std::make_shared<TestCartridge>() };
	Emulator GameBoy{ Rom };

	TimerEmulatorTests()
	{
		// Disable the internal ROM so the program runs from address 0
		GameBoy.GetMemoryMap().WriteByte(0xff50, 1);

		// Mode 1 (16 cycles per increment). LDH reads and writes all happen in the instruction's third
		// machine cycle, so the timings below, from the first DIV write, are exact
		Rom->SetBytes(0, {
			0xf3,				// DI
			0x3e, 0x05,			// LD A,5
			0xe0, 0x07,			// LDH (TAC),A		Enable timer, mode 1
			0xe0, 0x04,			// LDH (DIV),A		0: reset divider
			0xe0, 0x05,			// LDH (TIMA),A		12: TIMA = 5, then 6 at 16
			0xe0, 0x04,			// LDH (DIV),A		24: second half of the period, so 7
			0xf0, 0x05,			// LDH A,(TIMA)		36: 12 cycles after the DIV write
			0x47,				// LD B,A			48
			0xf0, 0x05,			// LDH A,(TIMA)		52: 28 cycles after the DIV write
			0xe0, 0x81,			// LDH (0x81),A
			0x78,				// LD A,B
			0xe0, 0x80,			// LDH (0x80),A
			0x18, 0xfe			// JR -2
		});

		GameBoy.GetFrame();
	}

	unsigned char ReadByte(unsigned short address) { return GameBoy.GetMemoryMap().ReadByte(address); }
};

TEST_F(TimerEmulatorTests, DivWriteInSecondHalfOfPeriodIncrementsCounter)
{
	EXPECT_EQ(7, ReadByte(0xff80));
}

TEST_F(TimerEmulatorTests, DivWriteRestartsThePeriod)
{
	// The next increment is 16 cycles after the DIV write
	EXPECT_EQ(7, ReadByte(0xff80));
	EXPECT_EQ(8, ReadByte(0xff81));
}
