#include "stdafx.h"
#include <gtest/gtest.h>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Spec references are to gb-apu-spec.md.

// ---- Square duty sequences (spec 4.1) ----
//
// Frequency 1984 gives a timer period of (2048 - 1984) * 4 = 256 T-cycles. The output is sampled in the middle of
// each period, which is robust against the (spec-uncertain) few-cycle delay before the first timer expiry.
// Sample k is taken after k expiries; the duty position advances by one per expiry from position 0 after power-on,
// so it must equal pattern[k % 8]. Samples 0 and 1 are not checked against the table: a freshly started pulse
// channel outputs 0 at first (sample 0, checked separately) and the first step after the first trigger is played
// as 0 (spec 4.1), so sample 1 is where sources are least precise.

namespace
{
	const int SquarePeriod = 256;
	const int SquareFrequency = 1984;
}

class ApuDutyTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuDutyTests, StepsThroughTheDutyPatternOnePositionPerTimerPeriod)
{
	ApuHarness h;
	int channel = 1 + GetParam() / 4;
	int duty = GetParam() % 4;
	h.StartSquare(channel, duty, 0xf0, SquareFrequency);

	h.Run(SquarePeriod / 2);

	for (int k = 1; k <= 24; k++)
	{
		h.Run(SquarePeriod);

		if (k >= 2)
		{
			EXPECT_EQ(DutyBit(duty, k) ? 15 : 0, h.Out(channel)) << "sample " << k;
		}
	}
}

TEST_P(ApuDutyTests, OutputsZeroBeforeTheFirstTimerExpiry)
{
	// spec 4.1: "A freshly started pulse channel always outputs digital 0 at first"
	ApuHarness h;
	int channel = 1 + GetParam() / 4;
	int duty = GetParam() % 4;
	h.StartSquare(channel, duty, 0xf0, SquareFrequency);

	h.Run(SquarePeriod / 2);

	EXPECT_EQ(0, h.Out(channel));
}

TEST_P(ApuDutyTests, ScalesTheHighLevelByTheEnvelopeVolume)
{
	ApuHarness h;
	int channel = 1 + GetParam() / 4;
	int duty = GetParam() % 4;
	h.StartSquare(channel, duty, 0x70, SquareFrequency);
	h.Run(SquarePeriod / 2);

	for (int k = 1; k <= 12; k++)
	{
		h.Run(SquarePeriod);

		if (k >= 2)
		{
			EXPECT_EQ(DutyBit(duty, k) ? 7 : 0, h.Out(channel)) << "sample " << k;
		}
	}
}

// Parameter = (channel - 1) * 4 + duty
INSTANTIATE_TEST_CASE_P(ApuSquare, ApuDutyTests, ::testing::Range(0, 8));

TEST(ApuSquareTests, RetriggerDoesNotResetTheDutyPosition)
{
	// spec 4.1: the position is only reset by powering the APU off. Three expiries -> position 3; after a retrigger
	// sample k must follow pattern[(3 + k) % 8], not pattern[k % 8].
	ApuHarness h;
	h.StartSquare(2, 2, 0xf0, SquareFrequency);
	h.Run(3 * SquarePeriod + SquarePeriod / 2);

	h.StartSquare(2, 2, 0xf0, SquareFrequency);
	h.Run(SquarePeriod / 2);

	for (int k = 1; k <= 12; k++)
	{
		h.Run(SquarePeriod);

		if (k >= 2)
		{
			EXPECT_EQ(DutyBit(2, 3 + k) ? 15 : 0, h.Out(2)) << "sample " << k;
		}
	}
}

TEST(ApuSquareTests, PowerCycleResetsTheDutyPosition)
{
	ApuHarness h;
	h.StartSquare(2, 2, 0xf0, SquareFrequency);
	h.Run(3 * SquarePeriod + SquarePeriod / 2);

	h.PowerOff();
	h.PowerOn();
	h.StartSquare(2, 2, 0xf0, SquareFrequency);
	h.Run(SquarePeriod / 2);

	for (int k = 1; k <= 12; k++)
	{
		h.Run(SquarePeriod);

		if (k >= 2)
		{
			EXPECT_EQ(DutyBit(2, k) ? 15 : 0, h.Out(2)) << "sample " << k;
		}
	}
}

TEST(ApuSquareTests, DutyBitsReadBackAndTheLengthBitsStayMasked)
{
	ApuHarness h;
	h.Write(Nr11, 0xc0 | 0x15);

	EXPECT_EQ(0xff, h.Read(Nr11));
	h.Write(Nr11, 0x40 | 0x3f);
	EXPECT_EQ(0x7f, h.Read(Nr11));
}

// ---- Square timer period (spec 4.1: (2048 - freq) * 4 T-cycles) ----
//
// With the 12.5% duty only position 7 is high, so consecutive rising edges are exactly eight timer periods apart,
// whatever the trigger delay.

class ApuSquarePeriodTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuSquarePeriodTests, RisingEdgesAreEightTimerPeriodsApartOnChannel2)
{
	ApuHarness h;
	int frequency = GetParam();
	long long period = (2048 - frequency) * 4;
	h.StartSquare(2, 0, 0xf0, frequency);

	auto edges = h.RisingEdges(2, 3, 40 * period + 100);

	ASSERT_EQ(3u, edges.size());
	EXPECT_EQ(8 * period, edges[1] - edges[0]);
	EXPECT_EQ(8 * period, edges[2] - edges[1]);
}

TEST_P(ApuSquarePeriodTests, RisingEdgesAreEightTimerPeriodsApartOnChannel1)
{
	ApuHarness h;
	int frequency = GetParam();
	long long period = (2048 - frequency) * 4;
	h.StartSquare(1, 0, 0xf0, frequency);

	auto edges = h.RisingEdges(1, 3, 40 * period + 100);

	ASSERT_EQ(3u, edges.size());
	EXPECT_EQ(8 * period, edges[1] - edges[0]);
	EXPECT_EQ(8 * period, edges[2] - edges[1]);
}

INSTANTIATE_TEST_CASE_P(ApuSquare, ApuSquarePeriodTests, ::testing::Values(0, 1, 1024, 1984, 2040, 2047));

TEST(ApuSquareTests, HighPulseOfTheTwelvePointFivePercentDutyLastsOneTimerPeriod)
{
	ApuHarness h;
	h.StartSquare(2, 0, 0xf0, 1500);
	long long period = (2048 - 1500) * 4;

	auto changes = h.Changes(2, 20 * period);

	ASSERT_GE(changes.size(), 2u);
	EXPECT_EQ(period, changes[1] - changes[0]);
}

// ---- Sweep (spec 8) ----
//
// Sweep clock n is at 24576 + 32768 * (n - 1). With pace 7 the first frequency update is at 221184, with a
// period of 229376 cycles after that, which leaves plenty of room for edge measurements before and after it.

namespace
{
	const long long FirstPace7Update = 24576 + 6 * 32768;

	void StartSweepChannel(ApuHarness& h, int nr10, int frequency)
	{
		h.Write(Nr10, nr10);
		h.StartSquare(1, 0, 0xf0, frequency);
	}

	long long RisingEdgeInterval(ApuHarness& h, long long limit)
	{
		auto edges = h.RisingEdges(1, 2, limit);

		return edges.size() == 2 ? edges[1] - edges[0] : -1;
	}
}

// highest frequency that does not overflow at trigger for shift 1-7 (blargg 06, spec 8.5)
class ApuSweepOverflowTests : public ::testing::TestWithParam<int>
{
protected:
	static int HighestSafeFrequency(int shift)
	{
		static const int table[] = { 0x555, 0x666, 0x71c, 0x787, 0x7c1, 0x7e0, 0x7f0 };

		return table[shift - 1];
	}
};

TEST_P(ApuSweepOverflowTests, TriggerKeepsTheChannelOnAtTheHighestSafeFrequency)
{
	int shift = GetParam();
	int pace[] = { 0, 1 };

	for (int p : pace)
	{
		ApuHarness h;
		StartSweepChannel(h, (p << 4) | shift, HighestSafeFrequency(shift));

		EXPECT_TRUE(h.Active(1)) << "pace " << p;
	}
}

TEST_P(ApuSweepOverflowTests, TriggerDisablesTheChannelOneAboveTheHighestSafeFrequencyEvenWithPaceZero)
{
	int shift = GetParam();
	int pace[] = { 0, 1 };

	for (int p : pace)
	{
		ApuHarness h;
		StartSweepChannel(h, (p << 4) | shift, HighestSafeFrequency(shift) + 1);

		EXPECT_FALSE(h.Active(1)) << "pace " << p;
	}
}

TEST_P(ApuSweepOverflowTests, NegateModeNeverOverflowsOnTrigger)
{
	int shift = GetParam();
	ApuHarness h;
	StartSweepChannel(h, 0x18 | shift, 0x7ff);

	EXPECT_TRUE(h.Active(1));
}

INSTANTIATE_TEST_CASE_P(ApuSweep, ApuSweepOverflowTests, ::testing::Range(1, 8));

TEST(ApuSweepTests, ShiftZeroDoesNoCalculationOnTrigger)
{
	// blargg 04 #3: with shift 0 there is no overflow check at trigger, even at the highest frequency.
	// (Sweep clocks with shift 0 are covered below.)
	ApuHarness h;
	StartSweepChannel(h, 0x10, 0x7ff);

	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, ShiftZeroNeverUpdatesTheFrequencyAtSweepClocks)
{
	// blargg 04 #11. With shift 0 the addend is the shadow itself, so f must be below 0x400 not to overflow.
	ApuHarness h;
	StartSweepChannel(h, 0x10, 500);

	h.RunTo(300000);
	ASSERT_TRUE(h.Active(1));

	long long expected = 8 * (2048 - 500) * 4;
	EXPECT_EQ(expected, RisingEdgeInterval(h, 3 * expected));
}

TEST(ApuSweepTests, ShiftZeroWithFrequencyBelow0x400SurvivesSweepClocksUnchanged)
{
	ApuHarness h;
	StartSweepChannel(h, 0x10, 0x3ff);

	h.RunTo(300000);

	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, ShiftZeroWithFrequencyOf0x400OrMoreOverflowsAtTheFirstSweepClock)
{
	// The calculation still runs at a clock when the period is non-zero: shadow + (shadow >> 0) = 0x800 > 0x7ff
	ApuHarness h;
	StartSweepChannel(h, 0x10, 0x400);
	ASSERT_TRUE(h.Active(1));

	h.RunTo(24575);
	EXPECT_TRUE(h.Active(1));
	h.RunTo(24576);
	EXPECT_FALSE(h.Active(1));
}

TEST(ApuSweepTests, ShiftZeroWithPeriodZeroNeverOverflows)
{
	ApuHarness h;
	StartSweepChannel(h, 0x00, 0x7ff);

	h.RunTo(300000);

	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, OverflowAtTheFirstSweepClockDisablesTheChannelForPaceOne)
{
	// f = 1000, shift 1: trigger calc 1500 is fine; the clock at 24576 updates to 1500, and the second
	// calculation (1500 + 750 = 2250) overflows and disables the channel.
	ApuHarness h;
	StartSweepChannel(h, 0x11, 1000);
	ASSERT_TRUE(h.Active(1));

	h.RunTo(24575);
	EXPECT_TRUE(h.Active(1));
	h.RunTo(24576);
	EXPECT_FALSE(h.Active(1));
}

TEST(ApuSweepTests, OverflowIsCheckedOnlyAtTheSecondSweepClockForPaceTwo)
{
	ApuHarness h;
	StartSweepChannel(h, 0x21, 1000);

	h.RunTo(57343);
	EXPECT_TRUE(h.Active(1));
	h.RunTo(57344);
	EXPECT_FALSE(h.Active(1));
}

TEST(ApuSweepTests, SecondCalculationAfterAnUpdateCanDisableTheChannel)
{
	// f = 700, shift 1, pace 1: clock 1 (24576) updates to 1050 (second calc 1575, fine); clock 2 (57344) updates
	// to 1575 and the second calculation (2362) overflows. blargg 04 #5.
	ApuHarness h;
	StartSweepChannel(h, 0x11, 700);

	h.RunTo(57343);
	EXPECT_TRUE(h.Active(1));
	h.RunTo(57344);
	EXPECT_FALSE(h.Active(1));
}

TEST(ApuSweepTests, PeriodZeroNeverChangesTheFrequencyNorOverflows)
{
	// shift 1, pace 0 at f = 1000: no sweep clocks do anything (an update would overflow at 24576)
	ApuHarness h;
	StartSweepChannel(h, 0x01, 1000);

	h.RunTo(150000);
	ASSERT_TRUE(h.Active(1));

	long long expected = 8 * (2048 - 1000) * 4;
	EXPECT_EQ(expected, RisingEdgeInterval(h, 3 * expected));
}

TEST(ApuSweepTests, AddModeUpdateRaisesTheFrequencyByShadowShiftedRight)
{
	// f = 1024, shift 2, pace 7: first update at 221184 gives 1024 + 256 = 1280
	ApuHarness h;
	StartSweepChannel(h, 0x72, 1024);

	long long before = 8 * (2048 - 1024) * 4;
	EXPECT_EQ(before, RisingEdgeInterval(h, 3 * before));
	ASSERT_LT(h.Now, FirstPace7Update);

	h.RunTo(FirstPace7Update + 8192);
	long long after = 8 * (2048 - 1280) * 4;
	EXPECT_EQ(after, RisingEdgeInterval(h, 3 * after));
	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, NegateModeUpdateLowersTheFrequency)
{
	// f = 1024, shift 2, negate, pace 7: 1024 - 256 = 768
	ApuHarness h;
	StartSweepChannel(h, 0x7a, 1024);

	h.RunTo(FirstPace7Update + 8192);

	long long after = 8 * (2048 - 768) * 4;
	EXPECT_EQ(after, RisingEdgeInterval(h, 3 * after));
	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, FrequencyIsNotChangedBeforeTheSweepTimerReloads)
{
	// pace 7: right up to the update the original frequency is still in use
	ApuHarness h;
	StartSweepChannel(h, 0x72, 1024);

	h.RunTo(FirstPace7Update - 100000);
	long long before = 8 * (2048 - 1024) * 4;

	EXPECT_EQ(before, RisingEdgeInterval(h, 3 * before));
}

TEST(ApuSweepTests, SweepUsesItsPrivateShadowCopyNotLaterWritesToNr13AndNr14)
{
	// blargg 05 #3: the shadow register is copied on trigger. Rewriting the channel frequency to 100 afterwards
	// does not feed the sweep, so the update still produces 1024 + 256 = 1280.
	ApuHarness h;
	StartSweepChannel(h, 0x72, 1024);
	h.Write(Nr13, 100);
	h.Write(Nr14, 0x00);

	h.RunTo(FirstPace7Update + 16384);

	long long after = 8 * (2048 - 1280) * 4;
	EXPECT_EQ(after, RisingEdgeInterval(h, 3 * after));
}

// ---- Negate-mode exit quirk (spec 8.4, blargg 05 #4-#6) ----

TEST(ApuSweepNegateTests, ClearingNegateAfterATriggerCalculationDisablesTheChannel)
{
	ApuHarness h;
	StartSweepChannel(h, 0x19, 1000); // pace 1, negate, shift 1: the trigger calculation is done in negate mode
	ASSERT_TRUE(h.Active(1));

	h.Write(Nr10, 0x11);

	EXPECT_FALSE(h.Active(1));
}

TEST(ApuSweepNegateTests, ClearingNegateAfterAClockCalculationDisablesTheChannel)
{
	// Trigger with shift 0 (no calculation), then raise the shift while staying in negate mode. The sweep clock
	// at 24576 calculates in negate mode; leaving negate mode afterwards disables the channel.
	ApuHarness h;
	StartSweepChannel(h, 0x18, 1000);
	ASSERT_TRUE(h.Active(1));
	h.Write(Nr10, 0x19);

	h.RunTo(24576 + 1000);
	ASSERT_TRUE(h.Active(1));
	h.Write(Nr10, 0x11);

	EXPECT_FALSE(h.Active(1));
}

TEST(ApuSweepNegateTests, ClearingNegateWithoutAnyNegateCalculationKeepsTheChannelOn)
{
	ApuHarness h;
	StartSweepChannel(h, 0x08, 1000); // negate, pace 0, shift 0: no calculation at all
	ASSERT_TRUE(h.Active(1));

	h.Write(Nr10, 0x00);

	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepNegateTests, ChangingPaceOrShiftWhileStayingInNegateModeKeepsTheChannelOn)
{
	ApuHarness h;
	StartSweepChannel(h, 0x19, 1000);

	h.Write(Nr10, 0x2a);

	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepNegateTests, RetriggerClearsTheNegateUsedFlag)
{
	ApuHarness h;
	StartSweepChannel(h, 0x19, 1000); // negate calculation done
	h.Write(Nr10, 0x08);              // still negate: no exit
	ASSERT_TRUE(h.Active(1));
	h.Write(Nr14, 0x80 | (1000 >> 8)); // retrigger with shift 0: no calculation, flag cleared
	ASSERT_TRUE(h.Active(1));

	h.Write(Nr10, 0x00);

	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepNegateTests, LeavingAddModeNeverDisablesTheChannel)
{
	ApuHarness h;
	StartSweepChannel(h, 0x11, 1000); // add mode
	ASSERT_TRUE(h.Active(1));

	h.Write(Nr10, 0x19); // enter negate
	EXPECT_TRUE(h.Active(1));
}
