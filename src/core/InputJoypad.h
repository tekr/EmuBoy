#pragma once

enum JoypadKey
{
	//InputJoypad::ReadRegister() relies on the below ordering in order to work correctly
	NoKey	= 0,
	Right	= 1 << 0,
	Left	= 1 << 1,
	Up		= 1 << 2,
	Down	= 1 << 3,
	Select	= 1 << 4,
	Start	= 1 << 5,
	A		= 1 << 6,
	B		= 1 << 7
};

class InputJoypad
{
	JoypadKey _keysDown;
	unsigned char _register;


public:
	InputJoypad();

	void SetKeysDown(JoypadKey keys) { _keysDown = keys; }

	void WriteRegister(unsigned char value) { _register = value; }

	unsigned char ReadRegister() const
	{
		return _register & 0x30 | 0xf & ~((~_register & 0x10 ? _keysDown & 0xf : 0) | (~_register & 0x20 ? (_keysDown & 0xf0) >> 4 : 0));
	}

	~InputJoypad();
};

