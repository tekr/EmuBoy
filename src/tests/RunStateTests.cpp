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

TEST(RunStateTest, TitleHasNoMessageAtFirst)
{
	RunState state;

	EXPECT_EQ("Emu Boy - TETRIS", state.WindowTitle("TETRIS", 1000));
}

TEST(RunStateTest, TitleShowsAMessageForAWhile)
{
	RunState state;
	state.ShowMessage("Saved state 1", 1000);

	EXPECT_EQ("Emu Boy - TETRIS - Saved state 1", state.WindowTitle("TETRIS", 1000));
	EXPECT_EQ("Emu Boy - TETRIS - Saved state 1", state.WindowTitle("TETRIS", 1000 + RunState::MessageDuration - 1));
	EXPECT_EQ("Emu Boy - TETRIS", state.WindowTitle("TETRIS", 1000 + RunState::MessageDuration));
}

TEST(RunStateTest, NewMessageReplacesTheLastOne)
{
	RunState state;
	state.ShowMessage("State slot 2", 0);
	state.ShowMessage("Saved state 2", 500000);

	EXPECT_EQ("Emu Boy - Saved state 2", state.WindowTitle("", 500000 + RunState::MessageDuration - 1));
}

TEST(RunStateTest, MessageFollowsThePausedState)
{
	RunState state;
	state.TogglePause();
	state.ShowMessage("Loaded state 3", 0);

	EXPECT_EQ("Emu Boy - TETRIS (Paused) - Loaded state 3", state.WindowTitle("TETRIS", 0));
}
