#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/RealTimeClock.h"

// Expectations from https://gbdev.io/pandocs/MBC3.html and the counter behaviour described at
// https://gbdev.io/pandocs/MBC3.html#the-clock-counter-registers

namespace
{
	const int CyclesPerSecond = RealTimeClock::CyclesPerSecond;

	class RealTimeClockTest : public testing::Test
	{
	public:
		RealTimeClock Clock;

		void Latch()
		{
			Clock.WriteLatch(0);
			Clock.WriteLatch(1);
		}

		unsigned char Read(int reg)
		{
			Latch();
			return Clock.ReadRegister(reg);
		}

		void Set(int seconds, int minutes, int hours, int days)
		{
			Clock.WriteRegister(RealTimeClock::Seconds, seconds);
			Clock.WriteRegister(RealTimeClock::Minutes, minutes);
			Clock.WriteRegister(RealTimeClock::Hours, hours);
			Clock.WriteRegister(RealTimeClock::DaysLow, days & 0xff);
			Clock.WriteRegister(RealTimeClock::DaysHigh, days >> 8);
		}

		int Days()
		{
			return Read(RealTimeClock::DaysLow) | (Read(RealTimeClock::DaysHigh) & RealTimeClock::DayHighBit) << 8;
		}

		bool DayCarry()
		{
			return (Read(RealTimeClock::DaysHigh) & RealTimeClock::DayCarryBit) != 0;
		}

		void RunSeconds(int seconds)
		{
			for (auto i = 0; i < seconds; i++) Clock.RunCycles(CyclesPerSecond);
		}
	};
}

TEST_F(RealTimeClockTest, StartsAtZero)
{
	for (auto reg = 0; reg < RealTimeClock::RegisterCount; reg++)
	{
		EXPECT_EQ(0, Read(reg));
	}
}

TEST_F(RealTimeClockTest, TicksOncePerSecondOfClockCycles)
{
	Clock.RunCycles(CyclesPerSecond - 1);
	EXPECT_EQ(0, Read(RealTimeClock::Seconds));

	Clock.RunCycles(1);
	EXPECT_EQ(1, Read(RealTimeClock::Seconds));

	Clock.RunCycles(3 * CyclesPerSecond + 100);
	EXPECT_EQ(4, Read(RealTimeClock::Seconds));
}

TEST_F(RealTimeClockTest, SecondsCarryIntoMinutesIntoHoursIntoDays)
{
	Set(59, 59, 23, 0);
	RunSeconds(1);

	EXPECT_EQ(0, Read(RealTimeClock::Seconds));
	EXPECT_EQ(0, Read(RealTimeClock::Minutes));
	EXPECT_EQ(0, Read(RealTimeClock::Hours));
	EXPECT_EQ(1, Days());
}

TEST_F(RealTimeClockTest, SecondsCarryIntoMinutes)
{
	Set(59, 10, 5, 0);
	RunSeconds(1);

	EXPECT_EQ(0, Read(RealTimeClock::Seconds));
	EXPECT_EQ(11, Read(RealTimeClock::Minutes));
	EXPECT_EQ(5, Read(RealTimeClock::Hours));
}

TEST_F(RealTimeClockTest, DayCounterCarriesIntoBit8)
{
	Set(59, 59, 23, 255);
	RunSeconds(1);

	EXPECT_EQ(256, Days());
	EXPECT_FALSE(DayCarry());
}

TEST_F(RealTimeClockTest, DayCounterOverflowSetsStickyCarry)
{
	Set(59, 59, 23, 511);
	RunSeconds(1);

	EXPECT_EQ(0, Days());
	EXPECT_TRUE(DayCarry());

	Set(59, 59, 23, 0);
	Clock.WriteRegister(RealTimeClock::DaysHigh, RealTimeClock::DayCarryBit);
	RunSeconds(1);
	EXPECT_EQ(1, Days());
	EXPECT_TRUE(DayCarry());

	Clock.WriteRegister(RealTimeClock::DaysHigh, 0);
	EXPECT_FALSE(DayCarry());
}

TEST_F(RealTimeClockTest, OutOfRangeSecondsWrapWithoutCarry)
{
	Set(60, 0, 0, 0);
	RunSeconds(3);
	EXPECT_EQ(63, Read(RealTimeClock::Seconds));

	RunSeconds(1);
	EXPECT_EQ(0, Read(RealTimeClock::Seconds));
	EXPECT_EQ(0, Read(RealTimeClock::Minutes));
}

TEST_F(RealTimeClockTest, OutOfRangeMinutesWrapWithoutCarry)
{
	Set(59, 63, 0, 0);
	RunSeconds(1);

	EXPECT_EQ(0, Read(RealTimeClock::Minutes));
	EXPECT_EQ(0, Read(RealTimeClock::Hours));
}

TEST_F(RealTimeClockTest, OutOfRangeHoursWrapWithoutCarry)
{
	Set(59, 59, 31, 0);
	RunSeconds(1);

	EXPECT_EQ(0, Read(RealTimeClock::Hours));
	EXPECT_EQ(0, Days());

	Set(59, 59, 24, 0);
	RunSeconds(1);
	EXPECT_EQ(25, Read(RealTimeClock::Hours));
}

TEST_F(RealTimeClockTest, RegistersKeepOnlyTheirBits)
{
	Set(0xff, 0xff, 0xff, 0xff);
	Clock.WriteRegister(RealTimeClock::DaysHigh, 0xff);

	EXPECT_EQ(0x3f, Read(RealTimeClock::Seconds));
	EXPECT_EQ(0x3f, Read(RealTimeClock::Minutes));
	EXPECT_EQ(0x1f, Read(RealTimeClock::Hours));
	EXPECT_EQ(0xff, Read(RealTimeClock::DaysLow));
	EXPECT_EQ(0xc1, Read(RealTimeClock::DaysHigh));
}

TEST_F(RealTimeClockTest, HaltStopsTheClock)
{
	Clock.RunCycles(CyclesPerSecond / 2);
	Clock.WriteRegister(RealTimeClock::DaysHigh, RealTimeClock::HaltBit);

	RunSeconds(5);
	Clock.AdvanceSeconds(1000);
	EXPECT_EQ(0, Read(RealTimeClock::Seconds));

	// The part-second before halting is kept
	Clock.WriteRegister(RealTimeClock::DaysHigh, 0);
	Clock.RunCycles(CyclesPerSecond / 2);
	EXPECT_EQ(1, Read(RealTimeClock::Seconds));
}

TEST_F(RealTimeClockTest, WritingSecondsResetsSubSecondCount)
{
	Clock.RunCycles(CyclesPerSecond - 10);
	Clock.WriteRegister(RealTimeClock::Seconds, 5);

	Clock.RunCycles(10);
	EXPECT_EQ(5, Read(RealTimeClock::Seconds));

	Clock.RunCycles(CyclesPerSecond - 10);
	EXPECT_EQ(6, Read(RealTimeClock::Seconds));
}

TEST_F(RealTimeClockTest, ReadsLatchedValues)
{
	RunSeconds(3);
	EXPECT_EQ(0, Clock.ReadRegister(RealTimeClock::Seconds));

	Latch();
	RunSeconds(2);
	EXPECT_EQ(3, Clock.ReadRegister(RealTimeClock::Seconds));
}

TEST_F(RealTimeClockTest, LatchesOnlyOnZeroThenOne)
{
	RunSeconds(1);
	Clock.WriteLatch(1);
	EXPECT_EQ(0, Clock.ReadRegister(RealTimeClock::Seconds));

	Clock.WriteLatch(0);
	Clock.WriteLatch(2);
	Clock.WriteLatch(1);
	EXPECT_EQ(0, Clock.ReadRegister(RealTimeClock::Seconds));

	Clock.WriteLatch(0);
	Clock.WriteLatch(0);
	Clock.WriteLatch(1);
	EXPECT_EQ(1, Clock.ReadRegister(RealTimeClock::Seconds));

	RunSeconds(1);
	Clock.WriteLatch(1);
	EXPECT_EQ(1, Clock.ReadRegister(RealTimeClock::Seconds));
}

TEST_F(RealTimeClockTest, WritesUpdateLatchedRegister)
{
	Clock.WriteRegister(RealTimeClock::Minutes, 42);
	EXPECT_EQ(42, Clock.ReadRegister(RealTimeClock::Minutes));
}

TEST_F(RealTimeClockTest, AdvanceSecondsCarriesThroughCounters)
{
	Set(30, 59, 23, 100);
	Clock.AdvanceSeconds(30 + 86400 * 2);

	EXPECT_EQ(0, Read(RealTimeClock::Seconds));
	EXPECT_EQ(0, Read(RealTimeClock::Minutes));
	EXPECT_EQ(0, Read(RealTimeClock::Hours));
	EXPECT_EQ(103, Days());
	EXPECT_FALSE(DayCarry());
}

TEST_F(RealTimeClockTest, AdvanceSecondsPastDay511SetsCarry)
{
	Set(0, 0, 0, 500);
	Clock.AdvanceSeconds(86400ull * 1000 + 1);

	EXPECT_EQ(1, Read(RealTimeClock::Seconds));
	EXPECT_EQ((500 + 1000) % 512, Days());
	EXPECT_TRUE(DayCarry());
}

TEST_F(RealTimeClockTest, AdvanceSecondsKeepsSubSecondCount)
{
	Clock.RunCycles(CyclesPerSecond - 1);
	Clock.AdvanceSeconds(1);
	Clock.RunCycles(1);

	EXPECT_EQ(2, Read(RealTimeClock::Seconds));
}

TEST_F(RealTimeClockTest, AdvanceSecondsMatchesTickingFromOutOfRangeCounters)
{
	const int seconds = 100000;

	RealTimeClock ticked;
	ticked.WriteRegister(RealTimeClock::Seconds, 62);
	ticked.WriteRegister(RealTimeClock::Minutes, 61);
	ticked.WriteRegister(RealTimeClock::Hours, 30);
	auto advanced = ticked;

	for (auto i = 0; i < seconds; i++) ticked.RunCycles(CyclesPerSecond);
	advanced.AdvanceSeconds(seconds);

	EXPECT_EQ(ticked.GetState().Counters, advanced.GetState().Counters);
}

TEST_F(RealTimeClockTest, StateRoundTrips)
{
	Set(1, 2, 3, 300);
	Clock.RunCycles(1234);
	Latch();
	Clock.WriteLatch(0);

	RealTimeClock restored;
	restored.SetState(Clock.GetState());

	EXPECT_EQ(Clock.GetState().Counters, restored.GetState().Counters);
	EXPECT_EQ(Clock.GetState().Latched, restored.GetState().Latched);
	EXPECT_EQ(1234, restored.GetState().CyclesIntoSecond);
	EXPECT_TRUE(restored.GetState().LatchArmed);
}

TEST_F(RealTimeClockTest, SetStateKeepsOnlyRegisterBits)
{
	RealTimeClock::State state;
	state.Counters.fill(0xff);
	state.Latched.fill(0xff);
	state.CyclesIntoSecond = 5 * CyclesPerSecond;

	Clock.SetState(state);
	auto& restored = Clock.GetState();

	const std::array<unsigned char, RealTimeClock::RegisterCount> masks{ { 0x3f, 0x3f, 0x1f, 0xff, 0xc1 } };
	EXPECT_EQ(masks, restored.Counters);
	EXPECT_EQ(masks, restored.Latched);
	EXPECT_EQ(CyclesPerSecond - 1, restored.CyclesIntoSecond);

	state.CyclesIntoSecond = -1;
	Clock.SetState(state);
	EXPECT_EQ(0, Clock.GetState().CyclesIntoSecond);
}
