#include "stdafx.h"
#include <gtest/gtest.h>
#include "ApuTestHelpers.h"

using namespace ApuTest;

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
	long long SquareTimerPeriod(int frequency)
	{
		return (2048 - frequency) * 4;
	}

	const int SquareFrequency = 1984;
	const long long SquarePeriod = SquareTimerPeriod(SquareFrequency);

	// Call right after a trigger. Expects samples 2..`samples` to follow the duty pattern from position
	// `startPosition` + 2 on, at `volume`.
	void ExpectDutySequence(ApuHarness& h, int channel, int duty, int volume, int startPosition, int samples)
	{
		h.Run(SquarePeriod / 2);

		for (int k = 1; k <= samples; k++)
		{
			h.Run(SquarePeriod);

			if (k >= 2)
			{
				EXPECT_EQ(DutyBit(duty, startPosition + k) ? volume : 0, h.Out(channel)) << "sample " << k;
			}
		}
	}
}

// Parameter = (channel - 1) * 4 + duty
class ApuDutyTests : public ::testing::TestWithParam<int>
{
protected:
	int Channel() const
	{
		return 1 + GetParam() / 4;
	}

	int Duty() const
	{
		return GetParam() % 4;
	}
};

TEST_P(ApuDutyTests, StepsThroughTheDutyPatternOnePositionPerTimerPeriod)
{
	ApuHarness h;
	h.StartSquare(Channel(), Duty(), 0xf0, SquareFrequency);

	ExpectDutySequence(h, Channel(), Duty(), 15, 0, 24);
}

TEST_P(ApuDutyTests, OutputsZeroBeforeTheFirstTimerExpiry)
{
	// spec 4.1: "A freshly started pulse channel always outputs digital 0 at first"
	ApuHarness h;
	h.StartSquare(Channel(), Duty(), 0xf0, SquareFrequency);

	h.Run(SquarePeriod / 2);

	EXPECT_EQ(0, h.Out(Channel()));
}

TEST_P(ApuDutyTests, ScalesTheHighLevelByTheEnvelopeVolume)
{
	ApuHarness h;
	h.StartSquare(Channel(), Duty(), 0x70, SquareFrequency);

	ExpectDutySequence(h, Channel(), Duty(), 7, 0, 12);
}

INSTANTIATE_TEST_CASE_P(ApuSquare, ApuDutyTests, ::testing::Range(0, 8));

TEST(ApuSquareTests, RetriggerDoesNotResetTheDutyPosition)
{
	// spec 4.1: the position is only reset by powering the APU off. Three expiries -> position 3; after a retrigger
	// sample k must follow pattern[(3 + k) % 8], not pattern[k % 8].
	ApuHarness h;
	h.StartSquare(2, 2, 0xf0, SquareFrequency);
	h.Run(3 * SquarePeriod + SquarePeriod / 2);

	h.StartSquare(2, 2, 0xf0, SquareFrequency);

	ExpectDutySequence(h, 2, 2, 15, 3, 12);
}

TEST(ApuSquareTests, PowerCycleResetsTheDutyPosition)
{
	ApuHarness h;
	h.StartSquare(2, 2, 0xf0, SquareFrequency);
	h.Run(3 * SquarePeriod + SquarePeriod / 2);

	h.PowerOff();
	h.PowerOn();
	h.StartSquare(2, 2, 0xf0, SquareFrequency);

	ExpectDutySequence(h, 2, 2, 15, 0, 12);
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

namespace
{
	void ExpectRisingEdgesEightTimerPeriodsApart(int channel, int frequency)
	{
		ApuHarness h;
		long long period = SquareTimerPeriod(frequency);
		h.StartSquare(channel, 0, 0xf0, frequency);

		auto edges = h.RisingEdges(channel, 3, 40 * period + 100);

		ASSERT_EQ(3u, edges.size());
		EXPECT_EQ(8 * period, edges[1] - edges[0]);
		EXPECT_EQ(8 * period, edges[2] - edges[1]);
	}
}

class ApuSquarePeriodTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuSquarePeriodTests, RisingEdgesAreEightTimerPeriodsApartOnChannel2)
{
	ExpectRisingEdgesEightTimerPeriodsApart(2, GetParam());
}

TEST_P(ApuSquarePeriodTests, RisingEdgesAreEightTimerPeriodsApartOnChannel1)
{
	ExpectRisingEdgesEightTimerPeriodsApart(1, GetParam());
}

INSTANTIATE_TEST_CASE_P(ApuSquare, ApuSquarePeriodTests, ::testing::Values(0, 1, 1024, 1984, 2040, 2047));

TEST(ApuSquareTests, HighPulseOfTheTwelvePointFivePercentDutyLastsOneTimerPeriod)
{
	ApuHarness h;
	h.StartSquare(2, 0, 0xf0, 1500);
	long long period = SquareTimerPeriod(1500);

	auto changes = h.Changes(2, 20 * period);

	ASSERT_GE(changes.size(), 2u);
	EXPECT_EQ(period, changes[1] - changes[0]);
}

// ---- Sweep (spec 8) ----
//
// With pace 7 the first frequency update is at sweep clock 7 and the next one 229376 cycles later, which leaves
// plenty of room for edge measurements before and after it.

namespace
{
	const long long FirstPace7Update = SweepClock(7);

	void StartSweepChannel(ApuHarness& h, int nr10, int frequency)
	{
		h.Write(Nr10, nr10);
		h.StartSquare(1, 0, 0xf0, frequency);
	}

	// CH1 plays duty 12.5% (StartSweepChannel), so rising edges are eight timer periods apart
	void ExpectChannel1Frequency(ApuHarness& h, int frequency)
	{
		long long interval = 8 * SquareTimerPeriod(frequency);
		auto edges = h.RisingEdges(1, 2, 3 * interval);

		ASSERT_EQ(2u, edges.size()) << "frequency " << frequency;
		EXPECT_EQ(interval, edges[1] - edges[0]) << "frequency " << frequency;
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

	ExpectChannel1Frequency(h, 500);
}

TEST(ApuSweepTests, ShiftZeroWithFrequencyBelow0x400SurvivesSweepClocks)
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

	h.ExpectTurnsOffAt(1, SweepClock(1));
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
	// f = 1000, shift 1: trigger calc 1500 is fine; sweep clock 1 updates to 1500, and the second
	// calculation (1500 + 750 = 2250) overflows and disables the channel.
	ApuHarness h;
	StartSweepChannel(h, 0x11, 1000);
	ASSERT_TRUE(h.Active(1));

	h.ExpectTurnsOffAt(1, SweepClock(1));
}

TEST(ApuSweepTests, OverflowIsCheckedOnlyAtTheSecondSweepClockForPaceTwo)
{
	ApuHarness h;
	StartSweepChannel(h, 0x21, 1000);

	h.ExpectTurnsOffAt(1, SweepClock(2));
}

TEST(ApuSweepTests, SecondCalculationAfterAnUpdateCanDisableTheChannel)
{
	// f = 700, shift 1, pace 1: clock 1 updates to 1050 (second calc 1575, fine); clock 2 updates
	// to 1575 and the second calculation (2362) overflows. blargg 04 #5.
	ApuHarness h;
	StartSweepChannel(h, 0x11, 700);

	h.ExpectTurnsOffAt(1, SweepClock(2));
}

TEST(ApuSweepTests, PeriodZeroNeverChangesTheFrequencyNorOverflows)
{
	// shift 1, pace 0 at f = 1000: no sweep clocks do anything (an update would overflow at sweep clock 1)
	ApuHarness h;
	StartSweepChannel(h, 0x01, 1000);

	h.RunTo(150000);
	ASSERT_TRUE(h.Active(1));

	ExpectChannel1Frequency(h, 1000);
}

TEST(ApuSweepTests, AddModeUpdateRaisesTheFrequencyByShadowShiftedRight)
{
	// f = 1024, shift 2, pace 7: the first update gives 1024 + 256 = 1280
	ApuHarness h;
	StartSweepChannel(h, 0x72, 1024);

	ExpectChannel1Frequency(h, 1024);
	ASSERT_LT(h.Now, FirstPace7Update);

	h.RunTo(FirstPace7Update + FrameSequencerPeriod);
	ExpectChannel1Frequency(h, 1280);
	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, NegateModeUpdateLowersTheFrequency)
{
	// f = 1024, shift 2, negate, pace 7: 1024 - 256 = 768
	ApuHarness h;
	StartSweepChannel(h, 0x7a, 1024);

	h.RunTo(FirstPace7Update + FrameSequencerPeriod);

	ExpectChannel1Frequency(h, 768);
	EXPECT_TRUE(h.Active(1));
}

TEST(ApuSweepTests, FrequencyIsNotChangedBeforeTheSweepTimerReloads)
{
	// pace 7: right up to the update the original frequency is still in use
	ApuHarness h;
	StartSweepChannel(h, 0x72, 1024);

	h.RunTo(FirstPace7Update - 100000);

	ExpectChannel1Frequency(h, 1024);
}

TEST(ApuSweepTests, SweepUsesItsPrivateShadowCopyNotLaterWritesToNr13AndNr14)
{
	// blargg 05 #3: the shadow register is copied on trigger. Rewriting the channel frequency to 100 afterwards
	// does not feed the sweep, so the update still produces 1024 + 256 = 1280.
	ApuHarness h;
	StartSweepChannel(h, 0x72, 1024);
	h.Write(Nr13, 100);
	h.Write(Nr14, 0x00);

	h.RunTo(FirstPace7Update + 2 * FrameSequencerPeriod);

	ExpectChannel1Frequency(h, 1280);
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
	// Trigger with shift 0 (no calculation), then raise the shift while staying in negate mode. Sweep clock 1
	// calculates in negate mode; leaving negate mode afterwards disables the channel.
	ApuHarness h;
	StartSweepChannel(h, 0x18, 1000);
	ASSERT_TRUE(h.Active(1));
	h.Write(Nr10, 0x19);

	h.RunTo(SweepClock(1) + 1000);
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
