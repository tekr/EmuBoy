#pragma once

enum JoypadKey
{
	//InputJoypad::ReadRegister() relies on the below ordering in order to work correctly
	NoKey	= 0,
	Right	= 1 << 0,
	Left	= 1 << 1,
	Up		= 1 << 2,
	Down	= 1 << 3,
	A		= 1 << 4,
	B		= 1 << 5,
	Select	= 1 << 6,
	Start	= 1 << 7
};

class Cpu;
class StateWriter;
class StateReader;

class InputJoypad
{
	Cpu* _cpu;

	JoypadKey _keysDown;
	unsigned char _register;

public:
	InputJoypad();

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	void SetKeysDown(JoypadKey keys);

	void WriteRegister(unsigned char value) { _register = value; }

	// Save states keep the keys held too, so that the front end pressing keys after a state loads interrupts the
	// game just as it would have when the state was saved
	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);

	unsigned char ReadRegister() const
	{
		return 0xc0 | _register & 0x30 | 0xf & ~((~_register & 0x10 ? _keysDown & 0xf : 0) |
												 (~_register & 0x20 ? (_keysDown & 0xf0) >> 4 : 0));
	}
};

