#include "InputMapper.h"
#include <stdexcept>
#include <string>

namespace
{
	// Hats only report -1, 0 and +1 on each axis (or about 0.7 on a diagonal on Windows), so any threshold between
	// 0 and 0.7 reads them the same
	const float HatThreshold = 0.5f;

	JoypadKey Combine(JoypadKey keys, JoypadKey more)
	{
		return static_cast<JoypadKey>(keys | more);
	}

	float AxisPosition(const std::map<GamepadAxis, float>& axes, GamepadAxis axis)
	{
		auto position = axes.find(axis);
		return position != axes.end() ? position->second : 0.f;
	}

	JoypadKey Directions(float x, float y, float threshold)
	{
		auto keys = JoypadKey::NoKey;
		if (x < -threshold) keys = Combine(keys, JoypadKey::Left);
		if (x > threshold) keys = Combine(keys, JoypadKey::Right);
		if (y < -threshold) keys = Combine(keys, JoypadKey::Up);
		if (y > threshold) keys = Combine(keys, JoypadKey::Down);
		return keys;
	}
}

InputMapper::InputMapper(const KeyMap& keys, const CommandMap& commands, GamepadLayout gamepad) :
	_layout{ std::move(gamepad) }
{
	for (auto& key : keys)
	{
		if (!_keys.insert(key).second) throw std::invalid_argument("Key " + std::to_string(key.first) + " is mapped twice");
	}

	for (auto& command : commands)
	{
		if (_keys.count(command.first) != 0 || !_commands.insert(command).second)
		{
			throw std::invalid_argument("Key " + std::to_string(command.first) + " is mapped twice");
		}
	}
}

Command InputMapper::KeyPressed(int key)
{
	if (!_keysHeld.insert(key).second) return Command::None;

	auto command = _commands.find(key);
	return command != _commands.end() ? command->second : Command::None;
}

void InputMapper::KeyReleased(int key)
{
	_keysHeld.erase(key);
}

void InputMapper::ButtonPressed(unsigned gamepad, unsigned button)
{
	_gamepads[gamepad].Buttons.insert(button);
}

void InputMapper::ButtonReleased(unsigned gamepad, unsigned button)
{
	_gamepads[gamepad].Buttons.erase(button);
}

void InputMapper::AxisMoved(unsigned gamepad, GamepadAxis axis, float position)
{
	_gamepads[gamepad].Axes[axis] = position;
}

void InputMapper::GamepadDisconnected(unsigned gamepad)
{
	_gamepads.erase(gamepad);
}

void InputMapper::ReleaseKeys()
{
	_keysHeld.clear();
}

JoypadKey InputMapper::GamepadKeys(const GamepadState& state) const
{
	auto keys = JoypadKey::NoKey;

	for (auto& button : _layout.Buttons)
	{
		if (state.Buttons.count(button.first) != 0) keys = Combine(keys, button.second);
	}

	keys = Combine(keys, Directions(AxisPosition(state.Axes, GamepadAxis::StickX),
		AxisPosition(state.Axes, GamepadAxis::StickY), _layout.StickDeadzone));

	auto hatY = AxisPosition(state.Axes, GamepadAxis::HatY);
	return Combine(keys, Directions(AxisPosition(state.Axes, GamepadAxis::HatX), _layout.HatUpIsPositive ? -hatY : hatY,
		HatThreshold));
}

JoypadKey InputMapper::GetKeysDown() const
{
	auto keys = JoypadKey::NoKey;

	for (auto key : _keysHeld)
	{
		auto mapping = _keys.find(key);
		if (mapping != _keys.end()) keys = Combine(keys, mapping->second);
	}

	for (auto& gamepad : _gamepads) keys = Combine(keys, GamepadKeys(gamepad.second));

	return keys;
}

bool InputMapper::IsHeld(Command command) const
{
	for (auto key : _keysHeld)
	{
		auto mapping = _commands.find(key);
		if (mapping != _commands.end() && mapping->second == command) return true;
	}

	return false;
}
