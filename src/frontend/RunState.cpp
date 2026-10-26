#include "RunState.h"

void RunState::ShowMessage(const std::string& message, int64_t now)
{
	_message = message;
	_messageUntil = now + MessageDuration;
}

std::string RunState::WindowTitle(const std::string& gameTitle, int64_t now) const
{
	std::string title = "Emu Boy";
	if (!gameTitle.empty()) title += " - " + gameTitle;

	if (_paused) title += " (Paused)";
	else if (_fastForwardHeld) title += " (Fast-forward)";

	if (!_message.empty() && now < _messageUntil) title += " - " + _message;

	return title;
}
