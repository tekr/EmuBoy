#include "stdafx.h"
#include "Mbc5Cartridge.h"

Mbc5Cartridge::Mbc5Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery, bool hasRumble) :
	Cartridge(std::move(rom), ramSize, hasBattery), _hasRumble(hasRumble)
{
}

void Mbc5Cartridge::RomWriteByte(unsigned short address, unsigned char value)
{
	if (address < 0x2000)
	{
		// MBC5 compares the whole byte, unlike MBC1 which only checks the lower nibble (per Gekkio's
		// Game Boy: Complete Technical Reference, https://gekkio.fi/files/gb-docs/gbctr.pdf)
		_ramEnabled = value == 0xa;
	}
	else if (address < 0x3000)
	{
		_romBank = (_romBank & 0x100) | value;
		SetRomBanks(0, _romBank);
	}
	else if (address < 0x4000)
	{
		_romBank = (_romBank & 0xff) | (value & 0x1) << 8;
		SetRomBanks(0, _romBank);
	}
	else if (address < 0x6000)
	{
		if (_hasRumble)
		{
			_rumbling = (value & 0x8) != 0;
			_ramBank = value & 0x7;
		}
		else
		{
			_ramBank = value & 0xf;
		}
	}
}

unsigned char Mbc5Cartridge::RamReadByte(unsigned short address) const
{
	return _ramEnabled ? ReadRamBank(_ramBank, address) : 0xff;
}

void Mbc5Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	if (_ramEnabled) WriteRamBank(_ramBank, address, value);
}
