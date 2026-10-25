#include "stdafx.h"
#include <gtest/gtest.h>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Length phases used below:
//   "second half": the next frame sequencer step clocks length (T in [0, 8192) or [16384, 24576), ...)
//   "first half":  the next step does not clock length         (T in [8192, 16384) or [24576, 32768), ...)

class ApuLengthTests : public ::testing::TestWithParam<int>
{
};

// ---- Length counter basics (spec 7.1) ----

TEST_P(ApuLengthTests, ExpiryAfterLoadedLengthClocksDisablesTheChannel)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 3, true);

	h.ExpectTurnsOffAt(channel, LengthClock(3));
}

TEST_P(ApuLengthTests, DoesNotExpireWhenLengthIsNotEnabled)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, false);

	h.RunTo(FrameSequencerEvent(600));
	EXPECT_TRUE(h.Active(channel));
}

TEST_P(ApuLengthTests, DisablingLengthEnableStopsTheCountdown)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, true);
	h.RunTo(1000); // second half: next step clocks length, so disabling causes no extra clock
	h.SetLengthEnable(channel, false);

	h.RunTo(FrameSequencerEvent(600));
	EXPECT_TRUE(h.Active(channel));
}

TEST_P(ApuLengthTests, ExpiryDoesNotTurnTheDacOff)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, true);
	h.RunTo(LengthClock(1));
	ASSERT_FALSE(h.Active(channel));

	EXPECT_EQ(0, h.Out(channel));
}

TEST_P(ApuLengthTests, ReloadingLengthWhilePlayingRestartsTheCount)
{
	// Counter 2, clocked to 1 at event 1. Reloading with 3 at 20000 (second half) gives expiry at clock 3 from then:
	// events 3, 5, 7.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, true);
	h.RunTo(20000);
	ASSERT_TRUE(h.Active(channel));
	h.SetLength(channel, 3);

	h.ExpectTurnsOffAt(channel, FrameSequencerEvent(7));
}

TEST_P(ApuLengthTests, ReloadingLengthDoesNotReEnableAnExpiredChannel)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, true);
	h.RunTo(LengthClock(1));
	ASSERT_FALSE(h.Active(channel));

	h.SetLength(channel, 5);

	EXPECT_FALSE(h.Active(channel));
}

TEST_P(ApuLengthTests, LoadingZeroMeansTheMaximumLength)
{
	// register value 0 -> 64 (256 for channel 3); expiry at length clock number Max
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Write(ChannelRegister(channel, 1), 0);
	h.Trigger(channel, true);

	h.ExpectTurnsOffAt(channel, LengthClock(MaxLength(channel)));
}

TEST_P(ApuLengthTests, TriggerDoesNotReloadANonZeroLength)
{
	// Length 3 (expiry at event 5). Retriggering at 3000 (second half) must not restore it to the maximum.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 3, true);
	h.RunTo(3000);
	h.Trigger(channel, true);

	h.ExpectTurnsOffAt(channel, LengthClock(3));
}

TEST_P(ApuLengthTests, TriggerWithLengthZeroReloadsTheMaximumEvenWithLengthDisabled)
{
	// Expire the length (event 1), trigger in the second half (20000) with length disabled -> counter = Max.
	// Enabling length afterwards, also in a second half (at 40000, next step 4), gives expiry after Max clocks
	// starting with event 5.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, true);
	h.RunTo(20000);
	ASSERT_FALSE(h.Active(channel));
	h.Trigger(channel, false);
	ASSERT_TRUE(h.Active(channel));
	h.RunTo(40000); // events 3 and 4 passed; next step is 4 -> still a second half; length is off
	h.SetLengthEnable(channel, true);

	// clock 1 = event 5 (40960); clock Max = event 2 * Max + 3
	h.ExpectTurnsOffAt(channel, FrameSequencerEvent(2 * MaxLength(channel) + 3));
}

// ---- Extra length clocking (spec 7.3) ----

TEST_P(ApuLengthTests, EnablingLengthInTheFirstHalfClocksItOnce)
{
	// Counter 2, length disabled. At 8292 (after event 1; next step 1 does not clock length) enabling gives an
	// immediate clock (-> 1), so the first real clock (event 3) expires it.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, false);
	h.RunTo(8292);
	h.SetLengthEnable(channel, true);
	ASSERT_TRUE(h.Active(channel));

	h.ExpectTurnsOffAt(channel, LengthClock(2));
}

TEST_P(ApuLengthTests, EnablingLengthInTheSecondHalfDoesNotClockIt)
{
	// Same, but enabling at 17000 (after event 2; next step 2 clocks length): counter stays 2, so expiry is the
	// second clock after that: events 3 and 5.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, false);
	h.RunTo(17000);
	h.SetLengthEnable(channel, true);

	h.RunTo(LengthClock(2) + 1000);
	EXPECT_TRUE(h.Active(channel));
	h.ExpectTurnsOffAt(channel, LengthClock(3));
}

TEST_P(ApuLengthTests, ExtraClockThatReachesZeroDisablesTheChannelImmediately)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, false);
	h.RunTo(8292);
	ASSERT_TRUE(h.Active(channel));

	h.SetLengthEnable(channel, true);

	EXPECT_FALSE(h.Active(channel));
}

TEST_P(ApuLengthTests, RewritingAnAlreadySetLengthEnableDoesNotClockAgain)
{
	// Only a disabled -> enabled change clocks (blargg 03 #4)
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, true);
	h.RunTo(8292);
	ASSERT_TRUE(h.Active(channel)); // event 1 clocked the counter from 2 to 1

	// Length is already enabled, so writing the same value again must not clock (a clock would expire it)
	h.SetLengthEnable(channel, true);
	EXPECT_TRUE(h.Active(channel));
	h.SetLengthEnable(channel, true);
	EXPECT_TRUE(h.Active(channel));
}

TEST_P(ApuLengthTests, TriggerWithZeroLengthInTheFirstHalfLoadsMaxMinusOne)
{
	// Expire (event 1), then trigger with length enabled at 8292 (first half): counter = Max - 1.
	// Clock n is at event 2n + 1, so expiry is at clock Max - 1: event 2 * Max - 1.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, true);
	h.RunTo(8292);
	ASSERT_FALSE(h.Active(channel));

	h.Trigger(channel, true);
	ASSERT_TRUE(h.Active(channel));

	h.ExpectTurnsOffAt(channel, FrameSequencerEvent(2 * MaxLength(channel) - 1));
}

TEST_P(ApuLengthTests, TriggerWithZeroLengthInTheSecondHalfLoadsTheFullMaximum)
{
	// Expire (event 1), trigger with length enabled at 20000 (second half): counter = Max, first clock is event 3
	// so expiry is event 2 * Max + 1.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, true);
	h.RunTo(20000);
	ASSERT_FALSE(h.Active(channel));

	h.Trigger(channel, true);
	ASSERT_TRUE(h.Active(channel));

	h.ExpectTurnsOffAt(channel, FrameSequencerEvent(2 * MaxLength(channel) + 1));
}

TEST_P(ApuLengthTests, TriggerThatClocksLengthOfOneClocksTwiceAndDoesNotFreeze)
{
	// blargg 03 #9: length 1, disabled; trigger+enable in the first half -> extra clock (0), reload (Max), so the
	// result is Max - 1 and the channel stays on.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, false);
	h.RunTo(8292);

	h.Trigger(channel, true);
	EXPECT_TRUE(h.Active(channel));

	h.ExpectTurnsOffAt(channel, FrameSequencerEvent(2 * MaxLength(channel) - 1));
}

INSTANTIATE_TEST_CASE_P(ApuChannels, ApuLengthTests, ::testing::Range(1, 5));

// ---- DACs (spec 6, blargg 02 #13-#15) ----

class ApuDacTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuDacTests, TriggerWithDacOnEnablesTheChannelAndItsOutput)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Trigger(channel, false);

	EXPECT_TRUE(h.Active(channel));
	EXPECT_GE(h.Out(channel), 0);
}

TEST_P(ApuDacTests, DacOffReportsMinusOne)
{
	ApuHarness h;
	int channel = GetParam();
	h.DisableDac(channel);

	EXPECT_EQ(-1, h.Out(channel));
}

TEST_P(ApuDacTests, DacOnButChannelInactiveReportsZero)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);

	EXPECT_EQ(0, h.Out(channel));
}

TEST_P(ApuDacTests, TurningTheDacOffDisablesAnActiveChannelImmediately)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Trigger(channel, false);
	ASSERT_TRUE(h.Active(channel));

	h.DisableDac(channel);

	EXPECT_FALSE(h.Active(channel));
	EXPECT_EQ(-1, h.Out(channel));
}

TEST_P(ApuDacTests, TurningTheDacBackOnDoesNotReEnableTheChannel)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Trigger(channel, false);
	h.DisableDac(channel);

	h.EnableDac(channel);

	EXPECT_FALSE(h.Active(channel));
	EXPECT_EQ(0, h.Out(channel));
}

TEST_P(ApuDacTests, TriggerWithDacOffDoesNotEnableTheChannel)
{
	ApuHarness h;
	int channel = GetParam();
	h.DisableDac(channel);
	h.Trigger(channel, false);

	EXPECT_FALSE(h.Active(channel));
	EXPECT_EQ(-1, h.Out(channel));
}

TEST_P(ApuDacTests, LengthIsStillReloadedByATriggerWhileTheDacIsOff)
{
	// blargg 03 #11/#12: other trigger effects still happen. Expired counter (0), DAC off, trigger with length
	// enabled in a second half (20000): counter = Max (the channel stays off, but length keeps being clocked).
	// After 10 more clocks the DAC is turned on and the channel retriggered at 180324 (second half): a non-zero
	// counter is left alone, so expiry is at Max - 10 further clocks, event 2 * Max + 1. (A counter that had stayed
	// 0 would have been reloaded here and expired 20 events later.)
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 1, true);
	h.RunTo(8300);
	ASSERT_FALSE(h.Active(channel));

	h.DisableDac(channel);
	h.RunTo(20000);
	h.Trigger(channel, true);
	ASSERT_FALSE(h.Active(channel));

	h.EnableDac(channel);
	h.RunTo(FrameSequencerEvent(22) + 100);
	h.Trigger(channel, true);
	ASSERT_TRUE(h.Active(channel));

	h.ExpectTurnsOffAt(channel, FrameSequencerEvent(2 * MaxLength(channel) + 1));
}

INSTANTIATE_TEST_CASE_P(ApuChannels, ApuDacTests, ::testing::Range(1, 5));

TEST(ApuDacRegisterTests, EnvelopeRegisterValuesWithVolumeZeroAndDecreaseTurnTheDacOff)
{
	// NRx2 & 0xf8 == 0 -> DAC off, for any period (spec 6)
	int values[] = { 0x00, 0x01, 0x07 };

	for (int channel : { 1, 2, 4 })
	{
		for (int value : values)
		{
			ApuHarness h;
			h.EnableDac(channel);
			h.Trigger(channel, false);

			h.Write(ChannelRegister(channel, 2), value);

			EXPECT_FALSE(h.Active(channel)) << "channel " << channel << " value " << value;
			EXPECT_EQ(-1, h.Out(channel)) << "channel " << channel << " value " << value;
		}
	}
}

TEST(ApuDacRegisterTests, EnvelopeRegisterValuesWithVolumeOrIncreaseKeepTheDacOn)
{
	int values[] = { 0x08, 0x10, 0x80, 0xf0 };

	for (int channel : { 1, 2, 4 })
	{
		for (int value : values)
		{
			ApuHarness h;
			h.Write(ChannelRegister(channel, 2), value);
			h.Trigger(channel, false);

			EXPECT_TRUE(h.Active(channel)) << "channel " << channel << " value " << value;
		}
	}
}

TEST(ApuDacRegisterTests, Channel3DacIsControlledByNr30Bit7Only)
{
	{
		ApuHarness h;
		h.Write(Nr30, 0x7f);
		h.Trigger(3, false);
		EXPECT_FALSE(h.Active(3));
		EXPECT_EQ(-1, h.Out(3));
	}

	{
		ApuHarness h;
		h.Write(Nr30, 0x80);
		h.Trigger(3, false);
		EXPECT_TRUE(h.Active(3));
	}
}

// ---- Volume envelope (spec 4.4) ----

class ApuEnvelopeTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuEnvelopeTests, InitialVolumeComesFromNrx2OnTrigger)
{
	int channel = GetParam();
	int volumes[] = { 1, 7, 15 };

	for (int volume : volumes)
	{
		ApuHarness h;
		h.StartEnvelopeChannel(channel, volume << 4);
		h.RunTo(3000);

		EXPECT_EQ(volume, h.EnvelopeVolume(channel)) << "volume " << volume;
	}
}

TEST_P(ApuEnvelopeTests, DecreasesByOneEveryEnvelopeClockWithPeriodOne)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0xf1); // volume 15, decrease, period 1

	EXPECT_EQ(15, h.EnvelopeVolumeBefore(channel, EnvelopeClock(1)));
	EXPECT_EQ(14, h.EnvelopeVolumeAfter(channel, EnvelopeClock(1)));
	EXPECT_EQ(14, h.EnvelopeVolumeBefore(channel, EnvelopeClock(2)));
	EXPECT_EQ(13, h.EnvelopeVolumeAfter(channel, EnvelopeClock(2)));
}

TEST_P(ApuEnvelopeTests, IncreasesByOneEveryEnvelopeClockWithPeriodOne)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0x79); // volume 7, increase, period 1

	EXPECT_EQ(7, h.EnvelopeVolumeBefore(channel, EnvelopeClock(1)));
	EXPECT_EQ(8, h.EnvelopeVolumeAfter(channel, EnvelopeClock(1)));
	EXPECT_EQ(9, h.EnvelopeVolumeAfter(channel, EnvelopeClock(2)));
}

TEST_P(ApuEnvelopeTests, PeriodThreeChangesVolumeEveryThirdEnvelopeClock)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0xf3); // volume 15, decrease, period 3

	EXPECT_EQ(15, h.EnvelopeVolumeBefore(channel, EnvelopeClock(3)));
	EXPECT_EQ(14, h.EnvelopeVolumeAfter(channel, EnvelopeClock(3)));
	EXPECT_EQ(14, h.EnvelopeVolumeBefore(channel, EnvelopeClock(6)));
	EXPECT_EQ(13, h.EnvelopeVolumeAfter(channel, EnvelopeClock(6)));
}

TEST_P(ApuEnvelopeTests, StopsAtFifteenWhenIncreasing)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0xd9); // volume 13, increase, period 1

	EXPECT_EQ(14, h.EnvelopeVolumeAfter(channel, EnvelopeClock(1)));
	EXPECT_EQ(15, h.EnvelopeVolumeAfter(channel, EnvelopeClock(2)));
	EXPECT_EQ(15, h.EnvelopeVolumeAfter(channel, EnvelopeClock(3)));
	EXPECT_EQ(15, h.EnvelopeVolumeAfter(channel, EnvelopeClock(20)));
}

TEST_P(ApuEnvelopeTests, StopsAtZeroWhenDecreasingAndTheChannelStaysEnabled)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0x21); // volume 2, decrease, period 1

	EXPECT_EQ(1, h.EnvelopeVolumeAfter(channel, EnvelopeClock(1)));
	EXPECT_EQ(0, h.EnvelopeVolumeAfter(channel, EnvelopeClock(2)));
	EXPECT_EQ(0, h.EnvelopeVolumeAfter(channel, EnvelopeClock(3)));
	EXPECT_EQ(0, h.EnvelopeVolumeAfter(channel, EnvelopeClock(20)));

	// the DAC (NRx2) is unchanged and the status bit is not cleared by the envelope reaching 0
	EXPECT_TRUE(h.Active(channel));
	EXPECT_EQ(0, h.Out(channel));
}

TEST_P(ApuEnvelopeTests, PeriodZeroNeverChangesTheVolumeWhenDecreasing)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0x90); // volume 9, decrease, period 0

	EXPECT_EQ(9, h.EnvelopeVolumeAfter(channel, EnvelopeClock(12)));
}

TEST_P(ApuEnvelopeTests, PeriodZeroNeverChangesTheVolumeWhenIncreasing)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0x58); // volume 5, increase, period 0

	EXPECT_EQ(5, h.EnvelopeVolumeAfter(channel, EnvelopeClock(12)));
}

TEST_P(ApuEnvelopeTests, RetriggerReloadsTheInitialVolume)
{
	ApuHarness h;
	int channel = GetParam();
	h.StartEnvelopeChannel(channel, 0xf1); // volume 15, decrease, period 1
	ASSERT_EQ(12, h.EnvelopeVolumeAfter(channel, EnvelopeClock(3)));

	h.RunTo(200000); // next step is 0, not the envelope step
	h.StartEnvelopeChannel(channel, 0xf1);
	h.RunTo(201000);

	EXPECT_EQ(15, h.EnvelopeVolume(channel));
}

INSTANTIATE_TEST_CASE_P(ApuChannels, ApuEnvelopeTests, ::testing::Values(1, 2, 4));
