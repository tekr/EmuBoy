#include "stdafx.h"
#include <gtest/gtest.h>
#include "../frontend/FramePacer.h"

namespace
{
	// The Game Boy's frame time in whole microseconds: 70224 clocks at 4194304 Hz, for 59.73 frames a second
	const int64_t FrameTime = 1000000LL * 70224 / 4194304;
}

TEST(FramePacerTest, FrameTimeIsGameBoyRate)
{
	EXPECT_EQ(16742, FrameTime);
}

TEST(FramePacerTest, WaitsForRestOfFrame)
{
	FramePacer pacer{ FrameTime };

	EXPECT_EQ(FrameTime - 5000, pacer.FrameDone(5000, 1));
}

TEST(FramePacerTest, KeepsToScheduleDespiteOversleeping)
{
	FramePacer pacer{ FrameTime };
	pacer.FrameDone(5000, 1);

	// Woke 1 ms late: the next frame is still due two frame times from the start, so the lateness isn't kept
	EXPECT_EQ(FrameTime - 6000, pacer.FrameDone(FrameTime + 1000 + 5000, 1));
}

TEST(FramePacerTest, RunsAtFrameRateOverManyFrames)
{
	FramePacer pacer{ FrameTime };
	int64_t now = 0;

	for (auto frame = 0; frame < 600; frame++)
	{
		now += 3000;
		now += pacer.FrameDone(now, 1) + 200;
	}

	// Each wait ends on schedule, so the 200 us oversleeps don't add up
	EXPECT_EQ(600 * FrameTime + 200, now);
}

TEST(FramePacerTest, CatchesUpAfterSmallDelay)
{
	FramePacer pacer{ FrameTime };

	// Two frames late: the next frames run without waiting until back on schedule
	EXPECT_EQ(0, pacer.FrameDone(3 * FrameTime, 1));
	EXPECT_EQ(0, pacer.FrameDone(3 * FrameTime + 1000, 1));
	EXPECT_EQ(0, pacer.FrameDone(3 * FrameTime + 2000, 1));
	EXPECT_EQ(FrameTime - 3000, pacer.FrameDone(3 * FrameTime + 3000, 1));
}

TEST(FramePacerTest, GivesUpCatchingUpAfterLongDelay)
{
	FramePacer pacer{ FrameTime };

	// Ten frames late (e.g. the window was dragged): restarts the schedule from now
	auto late = 11 * FrameTime;
	EXPECT_EQ(0, pacer.FrameDone(late, 1));
	EXPECT_EQ(FrameTime - 100, pacer.FrameDone(late + 100, 1));
}

TEST(FramePacerTest, FastForwardShortensFrames)
{
	FramePacer pacer{ FrameTime, 0 };
	pacer.FrameDone(FrameTime, 1);

	// The next frame was due at 2 frame times; at 8x speed the one after is due an eighth of a frame time later
	EXPECT_EQ(FrameTime, pacer.FrameDone(FrameTime, 8));
	EXPECT_EQ(FrameTime / 8, pacer.FrameDone(2 * FrameTime, 8));
}

TEST(FramePacerTest, StartTimeOffsetsSchedule)
{
	FramePacer pacer{ FrameTime, 1000000 };

	EXPECT_EQ(FrameTime, pacer.FrameDone(1000000, 1));
}

TEST(FramePacerTest, PresentsEveryFrameAtNormalSpeed)
{
	FramePacer pacer{ FrameTime };

	EXPECT_TRUE(pacer.ShouldPresent(100, 1));
	EXPECT_TRUE(pacer.ShouldPresent(200, 1));
	EXPECT_TRUE(pacer.ShouldPresent(300, 1));
}

TEST(FramePacerTest, PresentsAtNormalFrameRateWhenFastForwarding)
{
	FramePacer pacer{ FrameTime };
	auto presented = 0;

	// One second of frames at 8x
	for (int64_t now = 0; now < 1000000; now += FrameTime / 8)
	{
		if (pacer.ShouldPresent(now, 8)) presented++;
	}

	EXPECT_GE(presented, 59);
	EXPECT_LE(presented, 61);
}

TEST(FramePacerTest, PresentsAtOnceWhenFastForwardFallsBehind)
{
	FramePacer pacer{ FrameTime };
	EXPECT_TRUE(pacer.ShouldPresent(0, 8));

	// A frame that took longer than a frame time is shown, and the next is a frame time after it
	EXPECT_TRUE(pacer.ShouldPresent(3 * FrameTime, 8));
	EXPECT_FALSE(pacer.ShouldPresent(4 * FrameTime - 1, 8));
	EXPECT_TRUE(pacer.ShouldPresent(4 * FrameTime, 8));
}
