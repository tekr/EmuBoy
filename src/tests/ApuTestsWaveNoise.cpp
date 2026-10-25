#include "stdafx.h"
#include <gtest/gtest.h>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// ---- Wave channel (spec 4.2) ----

namespace
{
	// Wave RAM whose sample n is (n * 7 + 1) & 15, high nibble first
	int PatternSample(int n)
	{
		return (n * 7 + 1) & 15;
	}

	void LoadPatternWave(ApuHarness& h)
	{
		unsigned char bytes[16];

		for (int i = 0; i < 16; i++)
		{
			bytes[i] = static_cast<unsigned char>((PatternSample(2 * i) << 4) | PatternSample(2 * i + 1));
		}

		h.LoadWave(bytes);
	}

	// Timer period (2048 - 2016) * 2 = 64 T-cycles
	const int PatternFrequency = 2016;

	// Call half a timer period after a trigger. Sample k is taken after k expiries and must be pattern sample k % 32:
	// the first expiry moves to position 1 (spec 4.2 "Trigger behaviour").
	void ExpectPatternSamples(ApuHarness& h, int samples)
	{
		for (int k = 1; k <= samples; k++)
		{
			h.Run(64);
			ASSERT_EQ(PatternSample(k % 32), h.Out(3)) << "sample " << k;
		}
	}
}

class ApuWavePeriodTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuWavePeriodTests, RisingEdgesOfASingleLoudSampleAreThirtyTwoTimerPeriodsApart)
{
	// Timer period is (2048 - freq) * 2 T-cycles; sample 4 (high nibble of byte 2) is the only non-zero one.
	ApuHarness h;
	int frequency = GetParam();
	long long period = (2048 - frequency) * 2;
	unsigned char bytes[16] = {};
	bytes[2] = 0xf0;
	h.LoadWave(bytes);
	h.StartWave(1, frequency);

	auto edges = h.RisingEdges(3, 2, 100 * period + 100);

	ASSERT_EQ(2u, edges.size());
	EXPECT_EQ(32 * period, edges[1] - edges[0]);
}

INSTANTIATE_TEST_CASE_P(ApuWave, ApuWavePeriodTests, ::testing::Values(0, 1, 1024, 2040, 2047));

TEST(ApuWaveTests, PlaysSamplesInOrderHighNibbleFirstFromPositionOne)
{
	ApuHarness h;
	LoadPatternWave(h);
	h.StartWave(1, PatternFrequency);

	h.Run(32);
	EXPECT_EQ(0, h.Out(3)); // buffer is cleared by power-on and not refilled by the trigger

	ExpectPatternSamples(h, 64);
}

TEST(ApuWaveTests, SampleZeroIsSkippedUntilThePositionWraps)
{
	// Sample 0 is 15, everything else 0. Period 16: sample 0 is only reached on the 32nd expiry (~512 cycles).
	ApuHarness h;
	unsigned char bytes[16] = {};
	bytes[0] = 0xf0;
	h.LoadWave(bytes);
	h.StartWave(1, 2040);

	bool seenEarly = false;

	for (int i = 0; i < 500; i++)
	{
		h.Run(1);
		seenEarly = seenEarly || h.Out(3) > 0;
	}

	EXPECT_FALSE(seenEarly);

	bool seenAfterWrap = false;

	for (int i = 0; i < 80; i++)
	{
		h.Run(1);
		seenAfterWrap = seenAfterWrap || h.Out(3) > 0;
	}

	EXPECT_TRUE(seenAfterWrap);
}

TEST(ApuWaveTests, RetriggerAfterStoppingRestartsFromPositionOne)
{
	ApuHarness h;
	LoadPatternWave(h);
	h.StartWave(1, PatternFrequency);
	h.Run(32 + 5 * 64); // position 5
	h.Write(Nr30, 0x00);
	ASSERT_FALSE(h.Active(3));

	h.StartWave(1, PatternFrequency);
	ASSERT_TRUE(h.Active(3));
	h.Run(32);

	ExpectPatternSamples(h, 40);
}

class ApuWaveVolumeTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuWaveVolumeTests, OutputLevelCodeShiftsTheSample)
{
	// code 0 = mute (shift 4), 1 = 100%, 2 = 50%, 3 = 25%
	ApuHarness h;
	int sample = GetParam();
	unsigned char bytes[16];

	for (int i = 0; i < 16; i++)
	{
		bytes[i] = static_cast<unsigned char>((sample << 4) | sample);
	}

	h.LoadWave(bytes);
	int expected[] = { 0, sample, sample >> 1, sample >> 2 };

	for (int code = 0; code < 4; code++)
	{
		h.StartWave(code, 2040);
		h.Run(200);

		EXPECT_EQ(expected[code], h.Out(3)) << "code " << code << " sample " << sample;
	}
}

INSTANTIATE_TEST_CASE_P(ApuWave, ApuWaveVolumeTests, ::testing::Values(1, 5, 10, 11, 15));

TEST(ApuWaveTests, MutedChannelStillHasADacOn)
{
	ApuHarness h;
	h.StartWave(0, 2040);
	h.Run(200);

	EXPECT_TRUE(h.Active(3));
	EXPECT_EQ(0, h.Out(3));
}

// ---- Noise channel (spec 4.3) ----

namespace
{
	int NoiseDivisor(int code)
	{
		static const int divisors[] = { 8, 16, 32, 48, 64, 80, 96, 112 };

		return divisors[code];
	}

	// Checks the LFSR clock period (divisor << shift) exactly: every output change must land on a whole number of
	// periods after the trigger plus one common, small delay, and all the changes the model predicts must happen.
	void CheckNoiseTiming(int divisorCode, int shift)
	{
		ApuHarness h;
		h.StartNoise(shift, false, divisorCode);
		long long period = static_cast<long long>(NoiseDivisor(divisorCode)) << shift;
		const int steps = 120;

		LfsrModel model(false);
		std::vector<long long> expectedSteps;
		int previous = 0;

		for (int j = 1; j <= steps; j++)
		{
			model.Step();

			if (model.Output() != previous)
			{
				expectedSteps.push_back(j);
				previous = model.Output();
			}
		}

		ASSERT_GE(expectedSteps.size(), 4u);

		auto changes = h.Changes(4, steps * period + period / 2);

		ASSERT_EQ(expectedSteps.size(), changes.size());
		long long delay = changes[0] - expectedSteps[0] * period;
		EXPECT_GE(delay, 0);
		EXPECT_LT(delay, period / 2);

		for (size_t i = 0; i < changes.size(); i++)
		{
			EXPECT_EQ(expectedSteps[i] * period + delay, changes[i]) << "change " << i;
		}
	}

	bool EverNonZero(int shift, long long cycles, int stride)
	{
		ApuHarness h;
		h.StartNoise(shift, false, 0);

		for (long long t = 0; t < cycles; t += stride)
		{
			h.Run(stride);

			if (h.Out(4) > 0)
			{
				return true;
			}
		}

		return false;
	}

	// Divisor code 1 and shift 3 give an LFSR period of 128 T-cycles. Call right after a trigger: samples the middle
	// of each period and expects the output to follow LfsrModel, with more than `minimumOnes` ones so the comparison
	// is not vacuous.
	void ExpectOutputFollowsTheLfsrModel(ApuHarness& h, bool narrow, int steps, int minimumOnes)
	{
		LfsrModel model(narrow);
		int ones = 0;

		h.Run(64);
		ASSERT_EQ(0, h.Out(4));

		for (int step = 1; step <= steps; step++)
		{
			h.Run(128);
			model.Step();
			ones += model.Output() > 0 ? 1 : 0;
			ASSERT_EQ(model.Output(), h.Out(4)) << "step " << step;
		}

		ASSERT_GT(ones, minimumOnes);
	}
}

class ApuNoiseDivisorTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuNoiseDivisorTests, PeriodIsTheDivisorTableValueShiftedByTwo)
{
	CheckNoiseTiming(GetParam(), 2);
}

INSTANTIATE_TEST_CASE_P(ApuNoise, ApuNoiseDivisorTests, ::testing::Range(0, 8));

class ApuNoiseShiftTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuNoiseShiftTests, PeriodDoublesWithEachShiftStep)
{
	CheckNoiseTiming(1, GetParam());
}

INSTANTIATE_TEST_CASE_P(ApuNoise, ApuNoiseShiftTests, ::testing::Values(0, 1, 3, 6));

TEST(ApuNoiseTests, FifteenBitLfsrFollowsTheSpecifiedSequence)
{
	ApuHarness h;
	h.StartNoise(3, false, 1);

	ExpectOutputFollowsTheLfsrModel(h, false, 400, 20);
}

TEST(ApuNoiseTests, SevenBitLfsrFollowsTheSpecifiedSequence)
{
	// 400 steps cover more than three full 127-step periods
	ApuHarness h;
	h.StartNoise(3, true, 1);

	ExpectOutputFollowsTheLfsrModel(h, true, 400, 20);
}

TEST(ApuNoiseTests, TriggerResetsTheLfsr)
{
	ApuHarness h;
	h.StartNoise(3, false, 1);
	h.Run(64 + 100 * 128);

	h.StartNoise(3, false, 1);

	ExpectOutputFollowsTheLfsrModel(h, false, 80, 0);
}

TEST(ApuNoiseTests, ClockShiftOf14ReceivesNoClocks)
{
	// With a working shift of 14 (period 131072) the output would turn on at the 15th clock, ~1.97M cycles in.
	EXPECT_FALSE(EverNonZero(14, 2300000, 16384));
}

TEST(ApuNoiseTests, ClockShiftOf15ReceivesNoClocks)
{
	// ... and at ~3.9M cycles for shift 15 (period 262144)
	EXPECT_FALSE(EverNonZero(15, 4500000, 16384));
}

TEST(ApuNoiseTests, ClockShiftOf12DoesClockTheLfsr)
{
	// control for the two tests above: shift 12 (period 32768) turns the output on at the 15th clock (~492k)
	EXPECT_TRUE(EverNonZero(12, 700000, 4096));
}

// The tests above start with shift 14 or 15. Here the shift is changed while the channel is already running with a
// fast, changing LFSR, and the output must freeze.

namespace
{
	// Samples ChannelOutput(4) every 8 cycles for `count` samples; true if any differs from the first
	bool OutputChanges(ApuHarness& h, int count)
	{
		int first = h.Out(4);

		for (int i = 0; i < count; i++)
		{
			h.Run(8);

			if (h.Out(4) != first)
			{
				return true;
			}
		}

		return false;
	}
}

TEST(ApuNoiseTests, FastLfsrKeepsChangingWithShiftZero)
{
	// control: the same scenario without the shift write
	ApuHarness h;
	h.StartNoise(0, false, 0);
	h.Run(8 * 400);

	EXPECT_TRUE(OutputChanges(h, 2000));
}

class ApuNoiseFreezeTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuNoiseFreezeTests, ChangingTheShiftTo14Or15StopsTheLfsr)
{
	ApuHarness h;
	h.StartNoise(0, false, 0);
	h.Run(8 * 400);

	h.Write(Nr43, GetParam() << 4);
	h.Run(64); // let any clock that was already due happen

	EXPECT_FALSE(OutputChanges(h, 2000));
}

TEST_P(ApuNoiseFreezeTests, TheChannelStaysEnabledWhileFrozen)
{
	ApuHarness h;
	h.StartNoise(0, false, 0);
	h.Write(Nr43, GetParam() << 4);
	h.Run(100000);

	EXPECT_TRUE(h.Active(4));
}

INSTANTIATE_TEST_CASE_P(ApuNoise, ApuNoiseFreezeTests, ::testing::Values(14, 15));
