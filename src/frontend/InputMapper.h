#pragma once
#include "../core/InputJoypad.h"
#include <map>
#include <set>
#include <utility>
#include <vector>

// Front-end commands, as opposed to the Game Boy's own buttons
enum class Command
{
	None,
	TogglePause,
	FastForward,
	Reset,
	Screenshot,
	ToggleFullscreen
};

// The gamepad axes used for directions. The front end translates its own library's axes to these
enum class GamepadAxis
{
	StickX,
	StickY,
	HatX,
	HatY
};

// Which gamepad buttons press which Game Boy buttons, and how its axes are read
struct GamepadLayout
{
	std::vector<std::pair<unsigned, JoypadKey>> Buttons;

	// How far (as a fraction of full deflection) the stick must move before it counts as a direction, so a
	// stick resting slightly off centre doesn't press anything
	float StickDeadzone;

	// Hats report up as +1 on some platforms and -1 on others; the stick's Y axis is positive downwards everywhere
	bool HatUpIsPositive;
};

// Turns keyboard and gamepad events into the Game Boy buttons held and the front end's commands. Keys and buttons are
// plain numbers so the mapping doesn't depend on a particular input library. Each source is tracked separately, so
// releasing one key doesn't release a button another key or a gamepad still holds
class InputMapper
{
public:
	using KeyMap = std::vector<std::pair<int, JoypadKey>>;
	using CommandMap = std::vector<std::pair<int, Command>>;

	// Throws std::invalid_argument if a key is mapped twice, so a command can't clash with a game button
	InputMapper(const KeyMap& keys, const CommandMap& commands, GamepadLayout gamepad);

	// Returns the command a key starts, or Command::None. Repeats of a key already held return Command::None, so
	// toggles don't flicker with key repeat
	Command KeyPressed(int key);
	void KeyReleased(int key);

	void ButtonPressed(unsigned gamepad, unsigned button);
	void ButtonReleased(unsigned gamepad, unsigned button);

	// Position from -1 (left or up for the stick) to +1
	void AxisMoved(unsigned gamepad, GamepadAxis axis, float position);

	// Releases everything a gamepad held, as its release events won't come once it is unplugged
	void GamepadDisconnected(unsigned gamepad);

	// Releases every key and button, e.g. when the window loses focus and stops receiving release events
	void ReleaseAll();

	JoypadKey GetKeysDown() const;

	// Whether a key for a held command (like fast-forward) is down
	bool IsHeld(Command command) const;

private:
	struct GamepadState
	{
		std::set<unsigned> Buttons;
		std::map<GamepadAxis, float> Axes;
	};

	std::map<int, JoypadKey> _keys;
	std::map<int, Command> _commands;
	GamepadLayout _layout;

	std::set<int> _keysHeld;
	std::map<unsigned, GamepadState> _gamepads;

	JoypadKey GamepadKeys(const GamepadState& state) const;
};
