#include "stdafx.h"
#include "InputJoypad.h"


InputJoypad::InputJoypad(): _keysDown(JoypadKey::NoKey), _register(0x3f)
{
}


InputJoypad::~InputJoypad()
{
}
