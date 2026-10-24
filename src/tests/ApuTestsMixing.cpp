#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Spec references are to gb-apu-spec.md (section 9: mixing and output).
//
// Most tests use channel 3 playing a constant maximum sample (all wave RAM nibbles 15, 100% volume, fastest
// frequency), with channels 1, 2 and 4 off. That gives a constant DC level on whichever sides NR51 routes it to,
// which the high-pass filter then removes. The sign of the level depends on the DAC polarity the implementation
// chose (spec 9.3 lists both), so the tests only look at magnitude and at ratios.
// Samples are interleaved left, right.

namespace
{
	const int CyclesPerTenthOfASecond = 419430; // ~4410 frames at 44100 Hz

	void StartConstantChannel(ApuHarness& h, int nr50, int nr51)
	{
		unsigned char bytes[16];

		for (int i = 0; i < 16; i++)
		{
			bytes[i] = 0xff;
		}

		h.LoadWave(bytes);
		h.Write(Nr50, nr50);
		h.Write(Nr51, nr51);
		h.StartWave(1, 2047);
	}

	size_t Frames(const std::vector<short>& samples)
	{
		return samples.size() / 2;
	}

	int Sample(const std::vector<short>& samples, size_t frame, int side)
	{
		return samples[frame * 2 + side];
	}

	// side 0 = left, 1 = right
	int MaxAbs(const std::vector<short>& samples, size_t fromFrame, size_t toFrame, int side)
	{
		int result = 0;

		for (size_t frame = fromFrame; frame < toFrame && frame < Frames(samples); frame++)
		{
			result = std::max(result, std::abs(Sample(samples, frame, side)));
		}

		return result;
	}

	bool AllZeroFrom(const std::vector<short>& samples, size_t fromFrame)
	{
		return MaxAbs(samples, fromFrame, Frames(samples), 0) == 0 && MaxAbs(samples, fromFrame, Frames(samples), 1) == 0;
	}
}

// ---- Silence (spec 9.5: with all DACs off the output is 0) ----

TEST(ApuMixingTests, ProducesSilenceWhilePoweredOff)
{
	ApuHarness h(false);
	h.Run(CyclesPerTenthOfASecond);

	auto samples = h.Take();

	ASSERT_GT(Frames(samples), 4000u);
	EXPECT_TRUE(AllZeroFrom(samples, 0));
}

TEST(ApuMixingTests, ProducesSilenceWhenAllDacsAreOffEvenWithFullVolumeAndPanning)
{
	ApuHarness h;
	h.Write(Nr50, 0x77);
	h.Write(Nr51, 0xff);
	h.Run(CyclesPerTenthOfASecond);

	auto samples = h.Take();

	ASSERT_GT(Frames(samples), 4000u);
	EXPECT_TRUE(AllZeroFrom(samples, 0));
}

TEST(ApuMixingTests, BecomesSilentAfterPoweringOff)
{
	ApuHarness h;
	StartConstantChannel(h, 0x77, 0xff);
	h.Run(50000);
	h.Take();

	h.PowerOff();
	h.Run(CyclesPerTenthOfASecond);
	auto samples = h.Take();

	ASSERT_GT(Frames(samples), 4000u);
	EXPECT_TRUE(AllZeroFrom(samples, 3));
}

TEST(ApuMixingTests, BecomesSilentAfterTheLastDacIsTurnedOff)
{
	ApuHarness h;
	StartConstantChannel(h, 0x77, 0xff);
	h.Run(50000);
	h.Write(Nr30, 0x00);
	h.Take();

	h.Run(CyclesPerTenthOfASecond);
	auto samples = h.Take();

	ASSERT_GT(Frames(samples), 4000u);
	EXPECT_TRUE(AllZeroFrom(samples, 3));
}

TEST(ApuMixingTests, ProducesSilenceWhenNr51RoutesNothing)
{
	ApuHarness h;
	StartConstantChannel(h, 0x77, 0x00);
	h.Run(CyclesPerTenthOfASecond);

	auto samples = h.Take();

	ASSERT_GT(Frames(samples), 4000u);
	EXPECT_LE(MaxAbs(samples, 0, Frames(samples), 0), 1);
	EXPECT_LE(MaxAbs(samples, 0, Frames(samples), 1), 1);
}

// ---- Panning (spec 9.2) ----

class ApuPanningTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuPanningTests, RoutesChannel3ToTheSidesSelectedInNr51)
{
	// NR51 bit 6 = channel 3 left, bit 2 = channel 3 right
	int nr51 = GetParam();
	ApuHarness h;
	StartConstantChannel(h, 0x77, nr51);
	h.Run(CyclesPerTenthOfASecond);

	auto samples = h.Take();
	ASSERT_GT(Frames(samples), 4000u);

	int left = MaxAbs(samples, 5, 100, 0);
	int right = MaxAbs(samples, 5, 100, 1);

	if (nr51 & 0x40)
	{
		EXPECT_GT(left, 100);
	}
	else
	{
		EXPECT_LE(left, 1);
	}

	if (nr51 & 0x04)
	{
		EXPECT_GT(right, 100);
	}
	else
	{
		EXPECT_LE(right, 1);
	}
}

// right only, left only, both, neither, and masks that route only other channels
INSTANTIATE_TEST_CASE_P(ApuMixing, ApuPanningTests, ::testing::Values(0x04, 0x40, 0x44, 0x00, 0xbb, 0x3b, 0x88));

// ---- Master volume (spec 9.2: multiplier of volume + 1) ----

class ApuMasterVolumeTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuMasterVolumeTests, RightVolumeScalesTheOutputByVolumePlusOne)
{
	int volume = GetParam();

	ApuHarness reference;
	StartConstantChannel(reference, 0x00, 0x04);
	reference.Run(CyclesPerTenthOfASecond);
	auto base = reference.Take();

	ApuHarness h;
	StartConstantChannel(h, volume, 0x04);
	h.Run(CyclesPerTenthOfASecond);
	auto scaled = h.Take();

	ASSERT_EQ(base.size(), scaled.size());
	int unit = Sample(base, 20, 1);
	ASSERT_GT(std::abs(unit), 100);
	EXPECT_NEAR((volume + 1) * unit, Sample(scaled, 20, 1), 2 * (volume + 1) + 2);
	EXPECT_NEAR((volume + 1) * Sample(base, 200, 1), Sample(scaled, 200, 1), 2 * (volume + 1) + 2);
	EXPECT_EQ(0, MaxAbs(scaled, 0, Frames(scaled), 0));
}

TEST_P(ApuMasterVolumeTests, LeftVolumeScalesTheOutputByVolumePlusOne)
{
	int volume = GetParam();

	ApuHarness reference;
	StartConstantChannel(reference, 0x00, 0x40);
	reference.Run(CyclesPerTenthOfASecond);
	auto base = reference.Take();

	ApuHarness h;
	StartConstantChannel(h, volume << 4, 0x40);
	h.Run(CyclesPerTenthOfASecond);
	auto scaled = h.Take();

	ASSERT_EQ(base.size(), scaled.size());
	int unit = Sample(base, 20, 0);
	ASSERT_GT(std::abs(unit), 100);
	EXPECT_NEAR((volume + 1) * unit, Sample(scaled, 20, 0), 2 * (volume + 1) + 2);
	EXPECT_EQ(0, MaxAbs(scaled, 0, Frames(scaled), 1));
}

INSTANTIATE_TEST_CASE_P(ApuMixing, ApuMasterVolumeTests, ::testing::Range(0, 8));

TEST(ApuMixingTests, LeftAndRightVolumesAreIndependent)
{
	ApuHarness h;
	StartConstantChannel(h, 0x70, 0x44); // left x8, right x1
	h.Run(CyclesPerTenthOfASecond);
	auto samples = h.Take();

	int left = Sample(samples, 20, 0);
	int right = Sample(samples, 20, 1);

	ASSERT_GT(std::abs(right), 100);
	EXPECT_NEAR(8 * right, left, 20);
}

TEST(ApuMixingTests, SingleChannelAtMaximumLeavesHeadroomForTheOtherThree)
{
	// Four channels at full scale must fit in int16, so one channel can use at most about a quarter of the
	// peak-to-peak range, i.e. a step from DC +1 to -1 is at most ~16384.
	ApuHarness h;
	StartConstantChannel(h, 0x77, 0x44);
	h.Run(CyclesPerTenthOfASecond);
	auto samples = h.Take();

	int peak = std::max(MaxAbs(samples, 0, Frames(samples), 0), MaxAbs(samples, 0, Frames(samples), 1));

	EXPECT_LE(peak, 17000);
	EXPECT_GE(peak, 500);
}

// ---- High-pass filter (spec 9.5) ----

TEST(ApuMixingTests, ConstantDacLevelDecaysToZeroThroughTheHighPassFilter)
{
	ApuHarness h;
	StartConstantChannel(h, 0x07, 0x04);
	h.Run(4194304);
	auto samples = h.Take();
	ASSERT_GT(Frames(samples), 44000u);

	int early = MaxAbs(samples, 5, 100, 1);
	int late = MaxAbs(samples, Frames(samples) - 100, Frames(samples), 1);

	EXPECT_GT(early, 1000);
	EXPECT_LE(late, 16);
}

TEST(ApuMixingTests, DcLeftByAnExpiredChannelAlsoDecaysToZero)
{
	// Channel 3's length (1) expires at 8192 and its DAC stays on: a constant DC offset that the filter removes
	ApuHarness h;
	StartConstantChannel(h, 0x77, 0x44);
	h.SetLength(3, 1);
	h.Trigger(3, true);
	h.Run(4194304);
	ASSERT_FALSE(h.Active(3));
	auto samples = h.Take();
	ASSERT_GT(Frames(samples), 44000u);

	EXPECT_LE(MaxAbs(samples, Frames(samples) - 100, Frames(samples), 0), 16);
	EXPECT_LE(MaxAbs(samples, Frames(samples) - 100, Frames(samples), 1), 16);
}

// ---- Sample production rate (spec section 0, design: TakeSamples) ----

TEST(ApuSampleRateTests, OneSecondOfCyclesGivesTheSampleRateAt44100)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x80);
	apu.RunCycles(4194304);

	std::vector<short> samples;
	apu.TakeSamples(samples);

	EXPECT_EQ(0u, samples.size() % 2);
	EXPECT_NEAR(44100, static_cast<double>(samples.size() / 2), 1);
}

TEST(ApuSampleRateTests, OneSecondOfCyclesGivesTheSampleRateAt48000)
{
	Apu apu{ 48000 };
	apu.WriteRegister(Nr52, 0x80);
	apu.RunCycles(4194304);

	std::vector<short> samples;
	apu.TakeSamples(samples);

	EXPECT_EQ(0u, samples.size() % 2);
	EXPECT_NEAR(48000, static_cast<double>(samples.size() / 2), 1);
}

TEST(ApuSampleRateTests, PoweredOffApuStillProducesSamplesAtTheSameRate)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x00);
	apu.RunCycles(4194304);

	std::vector<short> samples;
	apu.TakeSamples(samples);

	EXPECT_NEAR(44100, static_cast<double>(samples.size() / 2), 1);
}

TEST(ApuSampleRateTests, ManySmallRunsAddUpToTheSameNumberOfFrames)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x80);
	long long frames = 0;
	long long cycles = 0;
	std::vector<short> samples;

	for (int i = 0; i < 20000; i++)
	{
		int chunk = 1 + (i * 37) % 419; // 1..419 T-cycles, averaging ~210
		apu.RunCycles(chunk);
		cycles += chunk;
		apu.TakeSamples(samples);
		frames += static_cast<long long>(samples.size() / 2);
	}

	double expected = static_cast<double>(cycles) * 44100.0 / 4194304.0;
	EXPECT_NEAR(expected, static_cast<double>(frames), 1.5);
}

TEST(ApuSampleRateTests, SamplesAreDeliveredIncrementallyAsCyclesRun)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x80);
	std::vector<short> samples;

	apu.RunCycles(4194304 / 60); // one video-frame's worth of cycles: ~735 audio frames
	apu.TakeSamples(samples);

	EXPECT_NEAR(735, static_cast<double>(samples.size() / 2), 2);
}
