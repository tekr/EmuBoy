#pragma once
#include <cstdint>
#include <string>

// Whether the emulator is paused or fast-forwarding, and the window title that shows it, along with any recent
// message. Pausing wins over fast-forward, so holding the fast-forward key while paused doesn't run the game
class RunState
{
	bool _paused{ false };
	bool _fastForwardHeld{ false };

	std::string _message;
	int64_t _messageUntil{ 0 };

public:
	// Fast-forward runs up to this many frames in the time of one, as fast as the computer allows
	static constexpr int FastForwardSpeed = 8;

	void TogglePause() { _paused = !_paused; }
	void SetFastForward(bool held) { _fastForwardHeld = held; }

	bool IsPaused() const { return _paused; }
	bool IsFastForwarding() const { return _fastForwardHeld && !_paused; }

	// How long a message stays in the title, in microseconds
	static constexpr int64_t MessageDuration = 2000000;

	// Frames to run per Game Boy frame time: 1 normally, FastForwardSpeed when fast-forwarding. Not meaningful
	// while paused, when no frames run
	int GetSpeed() const { return IsFastForwarding() ? FastForwardSpeed : 1; }

	// Shows a message, such as "Saved state 1", in the title for MessageDuration from now (in microseconds), in
	// place of any earlier one
	void ShowMessage(const std::string& message, int64_t now);

	// "Emu Boy - <game title>", with the state in brackets when paused or fast-forwarding, then " - <message>"
	// while there is one to show
	std::string WindowTitle(const std::string& gameTitle, int64_t now = 0) const;
};
