#include "stdafx.h"
#include "InputJoypad.h"
#include "Cpu.h"
#include "StateStream.h"

InputJoypad::InputJoypad(): _cpu(nullptr), _keysDown(JoypadKey::NoKey), _register(0)
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

void InputJoypad::WriteState(StateWriter& writer) const
{
	writer.BeginSection("JOYP");
	writer.WriteU8(_register);
	writer.WriteU8(static_cast<unsigned char>(_keysDown));
	writer.EndSection();
}

void InputJoypad::ReadState(StateReader& reader)
{
	reader.BeginSection("JOYP");
	_register = reader.ReadU8();
	_keysDown = static_cast<JoypadKey>(reader.ReadU8());
	reader.EndSection();
}
