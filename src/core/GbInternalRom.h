#pragma once
#include <cstdint>

class GbInternalRom
{
	static const uint8_t _data[];

public:
	static uint8_t ReadByte(uint16_t address)
	{
		return _data[address];
	}
};
