#pragma once
#include "../core/InputJoypad.h"
#include <map>
#include <set>
#include <string>
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
	ToggleFullscreen,
	LeaveFullscreen
};

// The gamepad axes used for directions. The front end translates its own library's axes to these
enum class GamepadAxis
{
	StickX,
	StickY,
	HatX,
	HatY
};

// Modifier keys held with a key, as flags. System is Cmd on a Mac and the Windows key on a PC
enum KeyModifiers : unsigned
{
	NoModifiers = 0,
	ControlModifier = 1 << 0,
	AltModifier = 1 << 1,
	ShiftModifier = 1 << 2,
	SystemModifier = 1 << 3
};

// A key pressed with exactly these modifiers
struct KeyChord
{
	int Key;
	unsigned Modifiers;
};

// Which gamepad buttons press which Game Boy buttons, and how its axes are read
struct GamepadLayout
{
	std::vector<std::pair<unsigned, JoypadKey>> Buttons;

	// The position the input library reports for an axis at full deflection (100 for SFML)
	float AxisRange;

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
	using CommandMap = std::vector<std::pair<KeyChord, Command>>;

	// Throws std::invalid_argument if a key or chord is mapped twice, or a game key is also used in a chord, so a
	// command can't clash with a game button
	InputMapper(const KeyMap& keys, const CommandMap& commands, GamepadLayout gamepad);

	// Returns the command a key starts with the modifiers held, or Command::None. Game keys ignore modifiers.
	// Repeats of a key already held return Command::None, so toggles don't flicker with key repeat
	Command KeyPressed(int key, unsigned modifiers = NoModifiers);
	void KeyReleased(int key);

	void ButtonPressed(unsigned gamepad, unsigned button);
	void ButtonReleased(unsigned gamepad, unsigned button);

	// Position from -AxisRange (left, or up for the stick) to +AxisRange
	void AxisMoved(unsigned gamepad, GamepadAxis axis, float position);

	// Releases everything a gamepad held, as its release events won't come once it is unplugged
	void GamepadDisconnected(unsigned gamepad);

	// Releases every key, e.g. when the window loses focus and stops receiving key events. Gamepads are left as
	// they are, as their events arrive whether or not the window has focus, and an axis held still sends none
	void ReleaseKeys();

	JoypadKey GetKeysDown() const;

	// Whether a chord for a held command (like fast-forward) is down, judged by the modifiers held when its key was
	// pressed
	bool IsHeld(Command command) const;

private:
	struct GamepadState
	{
		std::set<unsigned> Buttons;
		std::map<GamepadAxis, float> Axes;
	};

	std::map<int, JoypadKey> _keys;
	std::map<std::pair<int, unsigned>, Command> _commands;
	GamepadLayout _layout;

	// Each key held, with the modifiers held when it was pressed
	std::map<int, unsigned> _keysHeld;

	Command FindCommand(int key, unsigned modifiers) const;
	std::map<unsigned, GamepadState> _gamepads;

	JoypadKey GamepadKeys(const GamepadState& state) const;
};
