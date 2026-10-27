#include "stdafx.h"
#include "Controls.h"

namespace
{
	using Key = sf::Keyboard::Key;

	const float StickDeadzone = 0.5f;

	// SFML reports axis positions from -100 to 100
	const float SfmlAxisRange = 100.f;

	InputMapper::KeyMap GameKeys()
	{
		return
		{
			{ Key::D, JoypadKey::Right },
			{ Key::A, JoypadKey::Left },
			{ Key::W, JoypadKey::Up },
			{ Key::S, JoypadKey::Down },
			{ Key::Tab, JoypadKey::Select },
			{ Key::Return, JoypadKey::Start },
			{ Key::Comma, JoypadKey::B },
			{ Key::Period, JoypadKey::A }
		};
	}

	InputMapper::CommandMap CommandKeys()
	{
#ifdef __APPLE__
		// macOS takes F11 for Show Desktop before the app sees it; Ctrl+Cmd+F is the Mac's own fullscreen shortcut
		const KeyChord fullscreenKey{ Key::F, ControlModifier | SystemModifier };
#else
		const KeyChord fullscreenKey{ Key::F11, NoModifiers };
#endif

		return
		{
			{ { Key::P, NoModifiers }, Command::TogglePause },
			{ { Key::Escape, NoModifiers }, Command::LeaveFullscreen },
			{ { Key::Space, NoModifiers }, Command::FastForward },
			{ { Key::F2, NoModifiers }, Command::Reset },
			{ fullscreenKey, Command::ToggleFullscreen },
			{ { Key::F12, NoModifiers }, Command::Screenshot }
		};
	}

	// Xbox-style pads, laid out by position as on the Game Boy: the right face button (Xbox B) is A and the bottom
	// one (Xbox A) is B. Button numbers depend on the platform's driver, per SDL's game controller database
	// (https://github.com/mdqinc/SDL_GameControllerDB)
	GamepadLayout DefaultGamepadLayout()
	{
#ifdef __APPLE__
		// Xbox One and Series pads over Bluetooth number View and Menu 10 and 11
		const unsigned selectButton = 10;
		const unsigned startButton = 11;
#else
		// DirectInput on Windows (which SFML uses, falling back to WinMM) and the xpad driver on Linux number Back and
		// Start 6 and 7
		const unsigned selectButton = 6;
		const unsigned startButton = 7;
#endif

#ifdef __linux__
		// Linux reports hat positions as the joystick device gives them, with up negative; SFML converts Windows'
		// and macOS's hat angles with up positive
		const bool hatUpIsPositive = false;
#else
		const bool hatUpIsPositive = true;
#endif

		return
		{
			{ { 0, JoypadKey::B }, { 1, JoypadKey::A }, { selectButton, JoypadKey::Select }, { startButton, JoypadKey::Start } },
			SfmlAxisRange,
			StickDeadzone,
			hatUpIsPositive
		};
	}

	bool ToGamepadAxis(sf::Joystick::Axis axis, GamepadAxis& gamepadAxis)
	{
		switch (axis)
		{
		case sf::Joystick::X: gamepadAxis = GamepadAxis::StickX; return true;
		case sf::Joystick::Y: gamepadAxis = GamepadAxis::StickY; return true;
		case sf::Joystick::PovX: gamepadAxis = GamepadAxis::HatX; return true;
		case sf::Joystick::PovY: gamepadAxis = GamepadAxis::HatY; return true;
		default: return false;
		}
	}
}

InputMapper Controls::CreateInputMapper()
{
	return InputMapper{ GameKeys(), CommandKeys(), DefaultGamepadLayout() };
}

Command Controls::HandleEvent(const sf::Event& event, InputMapper& input)
{
	GamepadAxis axis;

	switch (event.type)
	{
	case sf::Event::KeyPressed:
		return input.KeyPressed(event.key.code, (event.key.control ? ControlModifier : NoModifiers) |
			(event.key.alt ? AltModifier : NoModifiers) | (event.key.shift ? ShiftModifier : NoModifiers) |
			(event.key.system ? SystemModifier : NoModifiers));

	case sf::Event::KeyReleased:
		input.KeyReleased(event.key.code);
		break;

	case sf::Event::JoystickButtonPressed:
		input.ButtonPressed(event.joystickButton.joystickId, event.joystickButton.button);
		break;

	case sf::Event::JoystickButtonReleased:
		input.ButtonReleased(event.joystickButton.joystickId, event.joystickButton.button);
		break;

	case sf::Event::JoystickMoved:
		if (ToGamepadAxis(event.joystickMove.axis, axis))
		{
			input.AxisMoved(event.joystickMove.joystickId, axis, event.joystickMove.position);
		}
		break;

	case sf::Event::JoystickDisconnected:
		input.GamepadDisconnected(event.joystickConnect.joystickId);
		break;

	default:
		break;
	}

	return Command::None;
}
