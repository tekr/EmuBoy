#include "stdafx.h"
#include <gtest/gtest.h>
#include "../frontend/InputMapper.h"
#include <stdexcept>

namespace
{
	enum TestKey { KeyRight = 1, KeyLeft, KeyUp, KeyDown, KeyA, KeyA2, KeyPause, KeyFastForward };

	const unsigned ButtonA = 1;
	const unsigned ButtonB = 0;
	const unsigned ButtonStart = 7;

	GamepadLayout Layout(bool hatUpIsPositive = true)
	{
		return { { { ButtonB, JoypadKey::B }, { ButtonA, JoypadKey::A }, { ButtonStart, JoypadKey::Start } }, 0.5f,
			hatUpIsPositive };
	}

	InputMapper Mapper(bool hatUpIsPositive = true)
	{
		return InputMapper{
			{ { KeyRight, JoypadKey::Right }, { KeyLeft, JoypadKey::Left }, { KeyUp, JoypadKey::Up },
				{ KeyDown, JoypadKey::Down }, { KeyA, JoypadKey::A }, { KeyA2, JoypadKey::A } },
			{ { KeyPause, Command::TogglePause }, { KeyFastForward, Command::FastForward } },
			Layout(hatUpIsPositive) };
	}

	JoypadKey Keys(int keys)
	{
		return static_cast<JoypadKey>(keys);
	}
}

TEST(InputMapperTest, NothingHeldAtStart)
{
	auto mapper = Mapper();

	EXPECT_EQ(JoypadKey::NoKey, mapper.GetKeysDown());
	EXPECT_FALSE(mapper.IsHeld(Command::FastForward));
}

TEST(InputMapperTest, KeysPressAndReleaseTheirButtons)
{
	auto mapper = Mapper();

	mapper.KeyPressed(KeyRight);
	mapper.KeyPressed(KeyA);
	EXPECT_EQ(Keys(JoypadKey::Right | JoypadKey::A), mapper.GetKeysDown());

	mapper.KeyReleased(KeyRight);
	EXPECT_EQ(JoypadKey::A, mapper.GetKeysDown());
}

TEST(InputMapperTest, UnmappedKeysDoNothing)
{
	auto mapper = Mapper();

	EXPECT_EQ(Command::None, mapper.KeyPressed(99));
	mapper.KeyReleased(98);

	EXPECT_EQ(JoypadKey::NoKey, mapper.GetKeysDown());
}

TEST(InputMapperTest, ButtonStaysDownWhileAnotherKeyForItIsHeld)
{
	auto mapper = Mapper();

	mapper.KeyPressed(KeyA);
	mapper.KeyPressed(KeyA2);
	mapper.KeyReleased(KeyA);

	EXPECT_EQ(JoypadKey::A, mapper.GetKeysDown());
}

TEST(InputMapperTest, CommandKeysReturnTheirCommandOncePerPress)
{
	auto mapper = Mapper();

	EXPECT_EQ(Command::TogglePause, mapper.KeyPressed(KeyPause));
	EXPECT_EQ(Command::None, mapper.KeyPressed(KeyPause));

	mapper.KeyReleased(KeyPause);
	EXPECT_EQ(Command::TogglePause, mapper.KeyPressed(KeyPause));
}

TEST(InputMapperTest, CommandKeysDontPressGameButtons)
{
	auto mapper = Mapper();

	mapper.KeyPressed(KeyPause);
	mapper.KeyPressed(KeyFastForward);

	EXPECT_EQ(JoypadKey::NoKey, mapper.GetKeysDown());
}

TEST(InputMapperTest, HeldCommandLastsUntilItsKeyIsReleased)
{
	auto mapper = Mapper();

	EXPECT_EQ(Command::FastForward, mapper.KeyPressed(KeyFastForward));
	EXPECT_TRUE(mapper.IsHeld(Command::FastForward));
	EXPECT_FALSE(mapper.IsHeld(Command::TogglePause));

	mapper.KeyReleased(KeyFastForward);
	EXPECT_FALSE(mapper.IsHeld(Command::FastForward));
}

TEST(InputMapperTest, RejectsKeyMappedTwice)
{
	EXPECT_THROW((InputMapper{ { { 1, JoypadKey::A }, { 1, JoypadKey::B } }, {}, Layout() }), std::invalid_argument);
	EXPECT_THROW((InputMapper{ { { 1, JoypadKey::A } }, { { 1, Command::Reset } }, Layout() }), std::invalid_argument);
	EXPECT_THROW((InputMapper{ {}, { { 2, Command::Reset }, { 2, Command::Screenshot } }, Layout() }),
		std::invalid_argument);
}

TEST(InputMapperTest, GamepadButtonsPressTheirButtons)
{
	auto mapper = Mapper();

	mapper.ButtonPressed(0, ButtonA);
	mapper.ButtonPressed(0, ButtonStart);
	EXPECT_EQ(Keys(JoypadKey::A | JoypadKey::Start), mapper.GetKeysDown());

	mapper.ButtonReleased(0, ButtonA);
	EXPECT_EQ(JoypadKey::Start, mapper.GetKeysDown());
}

TEST(InputMapperTest, UnmappedGamepadButtonsDoNothing)
{
	auto mapper = Mapper();

	mapper.ButtonPressed(0, 12);

	EXPECT_EQ(JoypadKey::NoKey, mapper.GetKeysDown());
}

TEST(InputMapperTest, KeyboardAndGamepadCombine)
{
	auto mapper = Mapper();

	mapper.KeyPressed(KeyA);
	mapper.ButtonPressed(0, ButtonA);
	mapper.ButtonPressed(0, ButtonB);
	mapper.KeyReleased(KeyA);

	EXPECT_EQ(Keys(JoypadKey::A | JoypadKey::B), mapper.GetKeysDown());
}

TEST(InputMapperTest, GamepadsAreTrackedSeparately)
{
	auto mapper = Mapper();

	mapper.ButtonPressed(0, ButtonA);
	mapper.ButtonPressed(1, ButtonA);
	mapper.ButtonReleased(0, ButtonA);

	EXPECT_EQ(JoypadKey::A, mapper.GetKeysDown());
}

TEST(InputMapperTest, StickPressesDirectionsBeyondDeadzone)
{
	auto mapper = Mapper();

	mapper.AxisMoved(0, GamepadAxis::StickX, -0.8f);
	EXPECT_EQ(JoypadKey::Left, mapper.GetKeysDown());

	mapper.AxisMoved(0, GamepadAxis::StickX, 0.8f);
	EXPECT_EQ(JoypadKey::Right, mapper.GetKeysDown());

	mapper.AxisMoved(0, GamepadAxis::StickX, 0.f);
	mapper.AxisMoved(0, GamepadAxis::StickY, -0.8f);
	EXPECT_EQ(JoypadKey::Up, mapper.GetKeysDown());

	mapper.AxisMoved(0, GamepadAxis::StickY, 0.8f);
	EXPECT_EQ(JoypadKey::Down, mapper.GetKeysDown());
}

TEST(InputMapperTest, StickInsideDeadzoneDoesNothing)
{
	auto mapper = Mapper();

	mapper.AxisMoved(0, GamepadAxis::StickX, 0.4f);
	mapper.AxisMoved(0, GamepadAxis::StickY, -0.5f);

	EXPECT_EQ(JoypadKey::NoKey, mapper.GetKeysDown());
}

TEST(InputMapperTest, StickDiagonalPressesTwoDirections)
{
	auto mapper = Mapper();

	mapper.AxisMoved(0, GamepadAxis::StickX, 0.7f);
	mapper.AxisMoved(0, GamepadAxis::StickY, 0.7f);

	EXPECT_EQ(Keys(JoypadKey::Right | JoypadKey::Down), mapper.GetKeysDown());
}

TEST(InputMapperTest, HatPressesDirections)
{
	auto mapper = Mapper(true);

	mapper.AxisMoved(0, GamepadAxis::HatX, 1.f);
	mapper.AxisMoved(0, GamepadAxis::HatY, 1.f);
	EXPECT_EQ(Keys(JoypadKey::Right | JoypadKey::Up), mapper.GetKeysDown());

	mapper.AxisMoved(0, GamepadAxis::HatX, -1.f);
	mapper.AxisMoved(0, GamepadAxis::HatY, -1.f);
	EXPECT_EQ(Keys(JoypadKey::Left | JoypadKey::Down), mapper.GetKeysDown());
}

TEST(InputMapperTest, HatWithUpNegative)
{
	auto mapper = Mapper(false);

	mapper.AxisMoved(0, GamepadAxis::HatY, -1.f);
	EXPECT_EQ(JoypadKey::Up, mapper.GetKeysDown());

	mapper.AxisMoved(0, GamepadAxis::HatY, 1.f);
	EXPECT_EQ(JoypadKey::Down, mapper.GetKeysDown());
}

TEST(InputMapperTest, HatDiagonalFromAngle)
{
	// Windows reports a hat as an angle, giving about 0.71 on each axis on a diagonal
	auto mapper = Mapper(true);

	mapper.AxisMoved(0, GamepadAxis::HatX, 0.71f);
	mapper.AxisMoved(0, GamepadAxis::HatY, 0.71f);

	EXPECT_EQ(Keys(JoypadKey::Right | JoypadKey::Up), mapper.GetKeysDown());
}

TEST(InputMapperTest, HatAndStickCombine)
{
	auto mapper = Mapper();

	mapper.AxisMoved(0, GamepadAxis::HatX, 1.f);
	mapper.AxisMoved(0, GamepadAxis::StickY, 1.f);

	EXPECT_EQ(Keys(JoypadKey::Right | JoypadKey::Down), mapper.GetKeysDown());
}

TEST(InputMapperTest, DisconnectReleasesThatGamepadOnly)
{
	auto mapper = Mapper();
	mapper.ButtonPressed(0, ButtonA);
	mapper.AxisMoved(0, GamepadAxis::StickX, 1.f);
	mapper.ButtonPressed(1, ButtonB);
	mapper.KeyPressed(KeyUp);

	mapper.GamepadDisconnected(0);

	EXPECT_EQ(Keys(JoypadKey::B | JoypadKey::Up), mapper.GetKeysDown());
}

TEST(InputMapperTest, ReconnectedGamepadStartsReleased)
{
	auto mapper = Mapper();
	mapper.AxisMoved(0, GamepadAxis::StickX, 1.f);
	mapper.GamepadDisconnected(0);

	mapper.ButtonPressed(0, ButtonA);

	EXPECT_EQ(JoypadKey::A, mapper.GetKeysDown());
}

TEST(InputMapperTest, ReleaseAllReleasesEverything)
{
	auto mapper = Mapper();
	mapper.KeyPressed(KeyLeft);
	mapper.KeyPressed(KeyFastForward);
	mapper.ButtonPressed(0, ButtonA);
	mapper.AxisMoved(1, GamepadAxis::HatX, 1.f);

	mapper.ReleaseAll();

	EXPECT_EQ(JoypadKey::NoKey, mapper.GetKeysDown());
	EXPECT_FALSE(mapper.IsHeld(Command::FastForward));

	// A key held through the focus change starts a command again when next pressed
	EXPECT_EQ(Command::FastForward, mapper.KeyPressed(KeyFastForward));
}
