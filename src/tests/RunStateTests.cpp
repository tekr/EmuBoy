#include "stdafx.h"
#include <gtest/gtest.h>
#include "../frontend/RunState.h"

TEST(RunStateTest, StartsRunningAtNormalSpeed)
{
	RunState state;

	EXPECT_FALSE(state.IsPaused());
	EXPECT_FALSE(state.IsFastForwarding());
	EXPECT_EQ(1, state.GetSpeed());
}

TEST(RunStateTest, PauseToggles)
{
	RunState state;

	state.TogglePause();
	EXPECT_TRUE(state.IsPaused());

	state.TogglePause();
	EXPECT_FALSE(state.IsPaused());
}

TEST(RunStateTest, FastForwardsWhileHeld)
{
	RunState state;

	state.SetFastForward(true);
	EXPECT_TRUE(state.IsFastForwarding());
	EXPECT_EQ(RunState::FastForwardSpeed, state.GetSpeed());

	state.SetFastForward(false);
	EXPECT_FALSE(state.IsFastForwarding());
	EXPECT_EQ(1, state.GetSpeed());
}

TEST(RunStateTest, FastForwardSpeedIsAboveNormal)
{
	EXPECT_GT(RunState::FastForwardSpeed, 1);
}

TEST(RunStateTest, PauseOverridesFastForward)
{
	RunState state;
	state.SetFastForward(true);

	state.TogglePause();
	EXPECT_FALSE(state.IsFastForwarding());

	// Still held when unpaused, so fast-forward carries on
	state.TogglePause();
	EXPECT_TRUE(state.IsFastForwarding());
}

TEST(RunStateTest, TitleNamesGame)
{
	RunState state;

	EXPECT_EQ("Emu Boy - TETRIS", state.WindowTitle("TETRIS"));
	EXPECT_EQ("Emu Boy", state.WindowTitle(""));
}

TEST(RunStateTest, TitleShowsState)
{
	RunState state;

	state.SetFastForward(true);
	EXPECT_EQ("Emu Boy - TETRIS (Fast-forward)", state.WindowTitle("TETRIS"));

	state.TogglePause();
	EXPECT_EQ("Emu Boy - TETRIS (Paused)", state.WindowTitle("TETRIS"));
	EXPECT_EQ("Emu Boy (Paused)", state.WindowTitle(""));
}
