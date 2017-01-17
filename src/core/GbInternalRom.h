#pragma once

class GbInternalRom
{
	static const unsigned char _data[];

public:
	static unsigned char ReadByte(unsigned short address)
	{
		return _data[address];
	}
};
