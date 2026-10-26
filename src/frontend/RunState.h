#pragma once
#include <string>

// Whether the emulator is paused or fast-forwarding, and the window title that shows it. Pausing wins over
// fast-forward, so holding the fast-forward key while paused doesn't run the game
class RunState
{
	bool _paused{ false };
	bool _fastForwardHeld{ false };

public:
	// Fast-forward runs up to this many frames in the time of one, as fast as the computer allows
	static const int FastForwardSpeed = 8;

	void TogglePause() { _paused = !_paused; }
	void SetFastForward(bool held) { _fastForwardHeld = held; }

	bool IsPaused() const { return _paused; }
	bool IsFastForwarding() const { return _fastForwardHeld && !_paused; }

	// Frames to run per Game Boy frame time: 1 normally, FastForwardSpeed when fast-forwarding. Not meaningful
	// while paused, when no frames run
	int GetSpeed() const { return IsFastForwarding() ? FastForwardSpeed : 1; }

	// "Emu Boy - <game title>", with the state in brackets when paused or fast-forwarding
	std::string WindowTitle(const std::string& gameTitle) const;
};
