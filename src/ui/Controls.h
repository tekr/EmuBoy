#pragma once
#include "SFML/Window/Event.hpp"
#include "../frontend/InputMapper.h"

// The default keyboard and gamepad controls, and the translation of SFML's input events for InputMapper
namespace Controls
{
	InputMapper CreateInputMapper();

	// Passes a keyboard or gamepad event to the mapper. Returns the command a key press starts, or Command::None
	Command HandleEvent(const sf::Event& event, InputMapper& input);
}
