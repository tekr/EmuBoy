#include "stdafx.h"
#include <gtest/gtest.h>
#include "../frontend/FramePacer.h"

namespace
{
	// The Game Boy's clock, and its frame of 154 lines of 456 clocks
	const int64_t ClockHz = 4194304;
	const uint64_t FrameClocks = 70224;

	// The frame time in whole microseconds, for 59.73 frames a second
	const int64_t FrameTime = 1000000LL * FrameClocks / ClockHz;

	// When the given number of clocks is due at the given speed, in microseconds from the start
	int64_t DueAtClocks(uint64_t clocks, int speed = 1) { return static_cast<int64_t>(clocks * 1000000 / (ClockHz * speed)); }
	int64_t DueAfterFrames(uint64_t frames) { return DueAtClocks(frames * FrameClocks); }

	FramePacer GameBoyPacer(int64_t start = 0) { return FramePacer{ ClockHz, FrameClocks, start }; }
}

TEST(FramePacerTest, FrameTimeIsGameBoyRate)
{
	EXPECT_EQ(16742, FrameTime);
}

TEST(FramePacerTest, WaitsForRestOfFrame)
{
	auto pacer = GameBoyPacer();

	EXPECT_EQ(FrameTime - 5000, pacer.FrameDone(5000, FrameClocks, 1));
}

TEST(FramePacerTest, KeepsToScheduleDespiteOversleeping)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(5000, FrameClocks, 1);

	// Woke 1 ms late: the next frame is still due two frame times from the start, so the lateness isn't kept
	EXPECT_EQ(DueAfterFrames(2) - (FrameTime + 1000 + 5000), pacer.FrameDone(FrameTime + 1000 + 5000, 2 * FrameClocks, 1));
}

TEST(FramePacerTest, RunsAtGameBoyClockOverManyFrames)
{
	auto pacer = GameBoyPacer();
	int64_t now = 0;

	for (uint64_t frame = 1; frame <= 600; frame++)
	{
		now += 3000;
		now += pacer.FrameDone(now, frame * FrameClocks, 1) + 200;
	}

	// Each wait ends on schedule, so neither the 200 us oversleeps nor the rounding of each frame to whole
	// microseconds adds up
	EXPECT_EQ(DueAfterFrames(600) + 200, now);
}

TEST(FramePacerTest, LongFrameWaitsForAllItsClocks)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(FrameTime, FrameClocks, 1);

	// The frame in which the LCD is turned on can last nearly two frames, until its first V-Blank: it's due when
	// its clocks are, not a frame time after the last frame
	const auto longFrameClocks = 2 * FrameClocks - 456;
	auto now = FrameTime + 3000;
	auto afterLong = FrameClocks + longFrameClocks;
	EXPECT_EQ(DueAtClocks(afterLong) - now, pacer.FrameDone(now, afterLong, 1));
	EXPECT_GT(DueAtClocks(afterLong) - now, FrameTime / 2 * 3);

	// The frames after it are back to a frame time apart
	now = DueAtClocks(afterLong) + 3000;
	EXPECT_EQ(DueAtClocks(afterLong + FrameClocks) - now, pacer.FrameDone(now, afterLong + FrameClocks, 1));
}

TEST(FramePacerTest, CatchesUpAfterSmallDelay)
{
	auto pacer = GameBoyPacer();

	// Two frames late: the next frames run without waiting until back on schedule
	EXPECT_EQ(0, pacer.FrameDone(3 * FrameTime, FrameClocks, 1));
	EXPECT_EQ(0, pacer.FrameDone(3 * FrameTime + 1000, 2 * FrameClocks, 1));
	EXPECT_EQ(0, pacer.FrameDone(3 * FrameTime + 2000, 3 * FrameClocks, 1));
	EXPECT_EQ(DueAfterFrames(4) - (3 * FrameTime + 3000), pacer.FrameDone(3 * FrameTime + 3000, 4 * FrameClocks, 1));
}

TEST(FramePacerTest, GivesUpCatchingUpAfterLongDelay)
{
	auto pacer = GameBoyPacer();

	// Ten frames late (e.g. the window was dragged): restarts the schedule from now
	auto late = 11 * FrameTime;
	EXPECT_EQ(0, pacer.FrameDone(late, FrameClocks, 1));
	EXPECT_EQ(FrameTime - 100, pacer.FrameDone(late + 100, 2 * FrameClocks, 1));
}

TEST(FramePacerTest, FastForwardShortensFrames)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(FrameTime, FrameClocks, 1);

	// At 8x speed, the clocks run after the speed changed are due eight times sooner; those before aren't affected
	auto changed = DueAfterFrames(1);
	EXPECT_EQ(DueAtClocks(FrameClocks, 8), pacer.FrameDone(changed, 2 * FrameClocks, 8));
	EXPECT_EQ(DueAtClocks(2 * FrameClocks, 8) - DueAtClocks(FrameClocks, 8),
		pacer.FrameDone(changed + DueAtClocks(FrameClocks, 8), 3 * FrameClocks, 8));
}

TEST(FramePacerTest, ReturningToNormalSpeedKeepsTheScheduleFromThere)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(0, FrameClocks, 8);
	auto fastFrameEnd = FrameTime / 8;

	// The next frame at normal speed is due a full frame time after the fast one was
	EXPECT_EQ(fastFrameEnd + FrameTime, pacer.FrameDone(0, 2 * FrameClocks, 1));
}

TEST(FramePacerTest, StartTimeOffsetsSchedule)
{
	auto pacer = GameBoyPacer(1000000);

	EXPECT_EQ(FrameTime, pacer.FrameDone(1000000, FrameClocks, 1));
}

TEST(FramePacerTest, RestartSchedulesFromNewClocks)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(FrameTime, 100 * FrameClocks, 1);

	// After a reset the new emulator's clocks start from 0 again
	pacer.Restart(5000000, 0);
	EXPECT_EQ(FrameTime - 1000, pacer.FrameDone(5000000 + 1000, FrameClocks, 1));
}

TEST(FramePacerTest, ClocksGoingBackRestartsSchedule)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(FrameTime, 100 * FrameClocks, 1);

	// A new emulator's first frame, without an explicit restart, still paces from now rather than waiting for its
	// clocks to pass the old emulator's
	EXPECT_EQ(0, pacer.FrameDone(2 * FrameTime, FrameClocks, 1));
	EXPECT_EQ(FrameTime - 100, pacer.FrameDone(2 * FrameTime + 100, 2 * FrameClocks, 1));
}

TEST(FramePacerTest, PausedWaitsAFrameAndResumesFromThere)
{
	auto pacer = GameBoyPacer();
	pacer.FrameDone(FrameTime, FrameClocks, 1);

	// Paused for a long time: no frames run, so the clocks stay put
	EXPECT_EQ(FrameTime, pacer.Paused(1000000, FrameClocks));
	EXPECT_EQ(FrameTime, pacer.Paused(1000000 + FrameTime, FrameClocks));

	// Unpaused a frame time after the last paused one: the first frame is due a frame time after that
	auto resumed = 1000000 + 2 * FrameTime;
	EXPECT_EQ(FrameTime - 2000, pacer.FrameDone(resumed + 2000, 2 * FrameClocks, 1));
}

TEST(FramePacerTest, PresentsEveryFrameAtNormalSpeed)
{
	auto pacer = GameBoyPacer();

	EXPECT_TRUE(pacer.ShouldPresent(100, 1));
	EXPECT_TRUE(pacer.ShouldPresent(200, 1));
	EXPECT_TRUE(pacer.ShouldPresent(300, 1));
}

TEST(FramePacerTest, PresentsAtNormalFrameRateWhenFastForwarding)
{
	auto pacer = GameBoyPacer();
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
	auto pacer = GameBoyPacer();
	EXPECT_TRUE(pacer.ShouldPresent(0, 8));

	// A frame that took longer than a frame time is shown, and the next is a frame time after it
	EXPECT_TRUE(pacer.ShouldPresent(3 * FrameTime, 8));
	EXPECT_FALSE(pacer.ShouldPresent(4 * FrameTime - 1, 8));
	EXPECT_TRUE(pacer.ShouldPresent(4 * FrameTime, 8));
}
