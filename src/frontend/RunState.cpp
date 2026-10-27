#include "RunState.h"

std::string RunState::WindowTitle(const std::string& gameTitle) const
{
	std::string title = "Emu Boy";
	if (!gameTitle.empty()) title += " - " + gameTitle;

	if (_paused) title += " (Paused)";
	else if (_fastForwardHeld) title += " (Fast-forward)";

	return title;
}
