#pragma once

// The CGB's undocumented registers $FF72-$FF75, which have no known purpose
// (https://gbdev.io/pandocs/CGB_Registers.html#undocumented-registers): $FF72-$FF74 are fully read/write in CGB
// mode, and $FF75 only in bits 4-6, its others reading 1. All start at 0
class UndocumentedRegisters
{
public:
	static const unsigned short FirstRegister = 0xff72;
	static const unsigned short LastRegister = 0xff75;
	static const int Count = LastRegister - FirstRegister + 1;

private:
	static const unsigned char WritableBits[Count];
	unsigned char _registers[Count]{};

public:
	unsigned char Read(unsigned short address) const
	{
		auto index = address - FirstRegister;
		return _registers[index] | static_cast<unsigned char>(~WritableBits[index]);
	}

	void Write(unsigned short address, unsigned char value)
	{
		auto index = address - FirstRegister;
		_registers[index] = value & WritableBits[index];
	}

	unsigned char* Data() { return _registers; }
	const unsigned char* Data() const { return _registers; }
};
