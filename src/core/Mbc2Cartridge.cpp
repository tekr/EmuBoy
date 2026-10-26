#include "stdafx.h"
#include "Mbc2Cartridge.h"
#include <algorithm>

Mbc2Cartridge::Mbc2Cartridge(std::vector<unsigned char>&& rom, bool hasBattery) :
	Cartridge(std::move(rom), RamSize, hasBattery)
{
}

void Mbc2Cartridge::RomWriteByte(unsigned short address, unsigned char value)
{
	// Only $0000-$3FFF has registers, with address bit 8 choosing between them
	if (address >= 0x4000) return;

	if (address & 0x100)
	{
		SetRomBanks(0, std::max(1, value & 0xf));
	}
	else
	{
		_ramEnabled = (value & 0xf) == 0xa;
	}
}

unsigned char Mbc2Cartridge::RamReadByte(unsigned short address) const
{
	// Only the lower nibble is stored; the upper one reads as 1s. The 512 bytes echo through $A000-$BFFF
	return _ramEnabled ? ReadRamBank(0, address) | 0xf0 : 0xff;
}

void Mbc2Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	if (_ramEnabled) WriteRamBank(0, address, value & 0xf);
}
