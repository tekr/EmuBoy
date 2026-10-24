#include "stdafx.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdlib>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Chunking must not matter: running N cycles in one call, in odd-sized pieces or one cycle at a time must give the
// same channel state, status register and sample stream. The scenario uses all four channels with length,
// envelope and sweep activity, so the comparison covers frame sequencer events, timer expiries and mixing.

namespace
{
	typedef std::array<int, 5> State; // ChannelOutput 1-4, NR52

	const int ScenarioCycles = 600000;

	void ConfigureScenario(ApuHarness& h)
	{
		h.Write(Nr50, 0x77);
		h.Write(Nr51, 0xff);

		// channel 1: duty 50%, length 16, volume 10 decreasing (period 2), sweep pace 2 / add / shift 3
		h.Write(Nr10, 0x23);
		h.Write(Nr11, 0x80 | 0x30);
		h.Write(Nr12, 0xa2);
		h.Write(Nr13, 1500 & 0xff);
		h.Write(Nr14, 0xc0 | (1500 >> 8));

		// channel 2: duty 25%, length 24, volume 5 increasing (period 1)
		h.Write(Nr21, 0x40 | 0x28);
		h.Write(Nr22, 0x59);
		h.Write(Nr23, 1900 & 0xff);
		h.Write(Nr24, 0xc0 | (1900 >> 8));

		// channel 3: a ramp, 50% volume, length 128
		for (int i = 0; i < 16; i++)
		{
			h.Write(static_cast<unsigned short>(WaveRam + i), (i << 4) | (15 - i));
		}

		h.Write(Nr30, 0x80);
		h.Write(Nr31, 0x80);
		h.Write(Nr32, 0x40);
		h.Write(Nr33, 1800 & 0xff);
		h.Write(Nr34, 0xc0 | (1800 >> 8));

		// channel 4: length 32, volume 12 decreasing (period 1), 15-bit, shift 3, divisor code 5
		h.Write(Nr41, 0x20);
		h.Write(Nr42, 0xc1);
		h.Write(Nr43, 0x35);
		h.Write(Nr44, 0xc0);
	}

	State Capture(ApuHarness& h)
	{
		State state = { { h.Out(1), h.Out(2), h.Out(3), h.Out(4), h.Read(Nr52) } };

		return state;
	}

	struct Reference
	{
		std::vector<State> States; // index = cycles elapsed since ConfigureScenario
		std::vector<short> Samples;
	};

	// Computed once: one cycle at a time
	const Reference& GetReference()
	{
		static Reference reference;
		static bool built = false;

		if (!built)
		{
			ApuHarness h;
			ConfigureScenario(h);
			h.Take();
			reference.States.push_back(Capture(h));

			std::vector<short> chunk;

			for (int t = 1; t <= ScenarioCycles; t++)
			{
				h.Device.RunCycles(1);
				reference.States.push_back(Capture(h));

				if (t % 1000 == 0)
				{
					h.Device.TakeSamples(chunk);
					reference.Samples.insert(reference.Samples.end(), chunk.begin(), chunk.end());
				}
			}

			h.Device.TakeSamples(chunk);
			reference.Samples.insert(reference.Samples.end(), chunk.begin(), chunk.end());
			built = true;
		}

		return reference;
	}

	void ExpectSameState(const State& expected, const State& actual, int cycles)
	{
		EXPECT_EQ(expected[0], actual[0]) << "channel 1 at " << cycles;
		EXPECT_EQ(expected[1], actual[1]) << "channel 2 at " << cycles;
		EXPECT_EQ(expected[2], actual[2]) << "channel 3 at " << cycles;
		EXPECT_EQ(expected[3], actual[3]) << "channel 4 at " << cycles;
		EXPECT_EQ(expected[4], actual[4]) << "NR52 at " << cycles;
	}

	void ExpectSameSamples(const std::vector<short>& expected, const std::vector<short>& actual)
	{
		ASSERT_EQ(expected.size(), actual.size());

		int worst = 0;

		for (size_t i = 0; i < expected.size(); i++)
		{
			worst = std::max(worst, std::abs(expected[i] - actual[i]));
		}

		EXPECT_LE(worst, 2);
	}
}

TEST(ApuRobustnessTests, ScenarioActuallyExercisesTheChannels)
{
	// guards against the comparisons below being vacuous
	const Reference& reference = GetReference();
	bool seenOutput[4] = { false, false, false, false };
	bool seenChange = false;

	for (const State& state : reference.States)
	{
		for (int i = 0; i < 4; i++)
		{
			seenOutput[i] = seenOutput[i] || state[i] > 0;
		}

		seenChange = seenChange || state[4] != reference.States[0][4];
	}

	for (int i = 0; i < 4; i++)
	{
		EXPECT_TRUE(seenOutput[i]) << "channel " << i + 1;
	}

	EXPECT_TRUE(seenChange); // length expiry changed NR52
	EXPECT_EQ(0xf0, reference.States.back()[4] & 0xf0);
}

TEST(ApuRobustnessTests, MidSizedChunksMatchSingleCycleSteps)
{
	const Reference& reference = GetReference();
	ApuHarness h;
	ConfigureScenario(h);
	h.Take();
	std::vector<short> samples;
	std::vector<short> chunk;

	for (int t = 0; t < ScenarioCycles;)
	{
		int size = std::min(997, ScenarioCycles - t);
		h.Device.RunCycles(size);
		t += size;
		ExpectSameState(reference.States[t], Capture(h), t);

		h.Device.TakeSamples(chunk);
		samples.insert(samples.end(), chunk.begin(), chunk.end());
	}

	ExpectSameSamples(reference.Samples, samples);
}

TEST(ApuRobustnessTests, RandomChunkSizesMatchSingleCycleSteps)
{
	const Reference& reference = GetReference();
	ApuHarness h;
	ConfigureScenario(h);
	h.Take();
	std::vector<short> samples;
	std::vector<short> chunk;
	unsigned seed = 12345;

	for (int t = 0; t < ScenarioCycles;)
	{
		seed = seed * 1103515245u + 12345u;
		int size = 1 + static_cast<int>((seed >> 16) % ((seed & 0x100) ? 3 : 9000));
		size = std::min(size, ScenarioCycles - t);
		h.Device.RunCycles(size);
		t += size;
		ExpectSameState(reference.States[t], Capture(h), t);

		h.Device.TakeSamples(chunk);
		samples.insert(samples.end(), chunk.begin(), chunk.end());
	}

	ExpectSameSamples(reference.Samples, samples);
}

TEST(ApuRobustnessTests, OneVeryLargeChunkMatchesSingleCycleSteps)
{
	const Reference& reference = GetReference();
	ApuHarness h;
	ConfigureScenario(h);
	h.Take();

	h.Device.RunCycles(ScenarioCycles);

	ExpectSameState(reference.States[ScenarioCycles], Capture(h), ScenarioCycles);
	ExpectSameSamples(reference.Samples, h.Take());
}

TEST(ApuRobustnessTests, SingleCycleStepsWithoutRegisterReadsMatchStepsWithReads)
{
	// Reads may trigger lazy catch-up inside the implementation. The reference reads NR52 after every cycle; this
	// run reads only at the end and must land in the same state.
	const Reference& reference = GetReference();
	ApuHarness h;
	ConfigureScenario(h);

	for (int t = 0; t < 100000; t++)
	{
		h.Device.RunCycles(1);
	}

	ExpectSameState(reference.States[100000], Capture(h), 100000);
}

// ---- TakeSamples and buffering ----

TEST(ApuRobustnessTests, TakeSamplesClearsTheCallersBufferWhenNothingNewWasProduced)
{
	Apu apu{ 44100 };
	std::vector<short> samples(10, 7);

	apu.TakeSamples(samples);

	EXPECT_TRUE(samples.empty());
}

TEST(ApuRobustnessTests, TakeSamplesReplacesPreviousContents)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x80);
	apu.RunCycles(100000);
	std::vector<short> samples(100000, 7);

	apu.TakeSamples(samples);

	EXPECT_NEAR(100000.0 * 44100.0 / 4194304.0, static_cast<double>(samples.size() / 2), 2);
}

TEST(ApuRobustnessTests, SecondTakeWithoutRunningReturnsNothing)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x80);
	apu.RunCycles(100000);
	std::vector<short> samples;

	apu.TakeSamples(samples);
	ASSERT_FALSE(samples.empty());
	apu.TakeSamples(samples);

	EXPECT_TRUE(samples.empty());
}

TEST(ApuRobustnessTests, LongRunWithRegularTakesProducesBoundedBatches)
{
	// 30 simulated seconds in 1/60 s steps: every batch is ~735 frames, nothing accumulates
	ApuHarness h;
	ConfigureScenario(h);
	std::vector<short> samples;
	size_t largest = 0;
	long long total = 0;

	for (int i = 0; i < 1800; i++)
	{
		h.Device.RunCycles(4194304 / 60);
		h.Device.TakeSamples(samples);
		largest = std::max(largest, samples.size());
		total += static_cast<long long>(samples.size());
	}

	EXPECT_LE(largest, 2u * 737u);
	EXPECT_NEAR(30.0 * 44100.0, static_cast<double>(total / 2), 30.0);
}

TEST(ApuRobustnessTests, ZeroCycleRunChangesNothing)
{
	ApuHarness h;
	ConfigureScenario(h);
	h.Device.RunCycles(5000);
	State before = Capture(h);
	h.Take();

	h.Device.RunCycles(0);

	ExpectSameState(before, Capture(h), 5000);
	EXPECT_TRUE(h.Take().empty());
}
