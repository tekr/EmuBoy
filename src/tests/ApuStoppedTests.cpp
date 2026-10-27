#include "stdafx.h"
#include <gtest/gtest.h>
#include <vector>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// While STOP has stopped the system clock the channels stand still, but the host's audio output keeps taking samples
// of their held output at its own rate

TEST(ApuStoppedTests, StoppedCyclesStillProduceSamplesAtTheSampleRate)
{
	ApuHarness h;
	std::vector<short> samples;

	h.Device.RunStoppedCycles(Apu::CpuClockHz);
	h.Device.TakeSamples(samples);

	EXPECT_NEAR(2 * 44100, static_cast<int>(samples.size()), 2);
}

TEST(ApuStoppedTests, ChannelsDontAdvanceWhileStopped)
{
	ApuHarness h;
	h.Write(Nr22, 0xf0);
	h.Write(Nr21, 63);		// Length 1: expires at the first length clock
	h.Write(Nr24, 0xc0);	// Trigger with length enabled

	h.Device.RunStoppedCycles(static_cast<int>(LengthClock(1)) * 10);
	EXPECT_EQ(2, h.Read(Nr52) & 2);

	h.Device.RunCycles(static_cast<int>(LengthClock(1)));
	EXPECT_EQ(0, h.Read(Nr52) & 2);
}

TEST(ApuStoppedTests, HeldOutputDecaysThroughTheHighPassFilter)
{
	ApuHarness h;
	h.Write(Nr50, 0x77);
	h.Write(Nr51, 0xff);
	h.Write(Nr22, 0xf0);
	h.Write(Nr24, 0x80);
	h.Device.RunCycles(1000);

	std::vector<short> samples;
	h.Device.TakeSamples(samples);
	h.Device.RunStoppedCycles(Apu::CpuClockHz);
	h.Device.TakeSamples(samples);

	// The held output carries on and decays away, rather than being cut off
	ASSERT_FALSE(samples.empty());
	EXPECT_NE(0, samples.front());
	EXPECT_EQ(0, samples.back());
}
