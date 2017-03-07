#include "InputJoypad.h"
#include "Cpu.h"

InputJoypad::InputJoypad(): _cpu(nullptr), _keysDown(JoypadKey::NoKey), _register(0x3f)
{
}

void InputJoypad::SetKeysDown(JoypadKey keys)
{
	auto oldRegister = ReadRegister();
	_keysDown = keys;

	// Interrupt only triggered on negative transition of ANDed button inputs
	if ((oldRegister & 0xf) == 0xf && ((ReadRegister() & 0xf) != 0xf))
	{
		_cpu->RequestInterrupt(InterruptFlags::JoypadInt);
	}
}
