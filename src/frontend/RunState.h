#pragma once
#include <string>

// Whether the emulator is paused or fast-forwarding, and the window title that shows it. Pausing wins over
// fast-forward, so holding the fast-forward key while paused doesn't run the game
class RunState
{
	bool _paused{ false };
	bool _fastForwardHeld{ false };

public:
	// Fast-forward runs the game up to this many times normal speed, as far as the computer allows
	static constexpr int FastForwardSpeed = 8;

	void TogglePause() { _paused = !_paused; }
	void SetFastForward(bool held) { _fastForwardHeld = held; }

	bool IsPaused() const { return _paused; }
	bool IsFastForwarding() const { return _fastForwardHeld && !_paused; }

	// How many times normal speed to run the emulated clock: 1 normally, FastForwardSpeed when fast-forwarding. Not
	// meaningful while paused, when nothing runs
	int GetSpeed() const { return IsFastForwarding() ? FastForwardSpeed : 1; }

	// "Emu Boy - <game title>", with the state in brackets when paused or fast-forwarding
	std::string WindowTitle(const std::string& gameTitle) const;
};
