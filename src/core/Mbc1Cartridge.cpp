#include "stdafx.h"
#include "Mbc1Cartridge.h"
#include "StateStream.h"
#include <algorithm>

namespace
{
	const size_t MulticartSize = 1 << 20;
	const size_t MulticartGameSize = 1 << 18;
	const size_t LogoOffset = 0x104;
	const size_t LogoSize = 0x30;
}

Mbc1Cartridge::Mbc1Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery, bool isMulticart) :
	Cartridge(std::move(rom), ramSize, hasBattery), _bank2Shift(isMulticart ? 4 : 5)
{
	UpdateRomBanks();
}

bool Mbc1Cartridge::IsMulticart(const std::vector<unsigned char>& rom)
{
	// Detected as emulators commonly do, by a second game's boot logo in a 1 MB ROM
	if (rom.size() != MulticartSize) return false;

	auto logo = rom.begin() + LogoOffset;
	return std::equal(logo, logo + LogoSize, logo + MulticartGameSize);
}

void Mbc1Cartridge::UpdateRomBanks()
{
	auto upperBits = _bank2 << _bank2Shift;
	auto lowerBits = _bank1 & ((1 << _bank2Shift) - 1);

	SetRomBanks(_mode1 ? upperBits : 0, upperBits | lowerBits);
}

void Mbc1Cartridge::ResetRegisters()
{
	_bank1 = 1;
	_bank2 = 0;
	_mode1 = false;
	_ramEnabled = false;
	UpdateRomBanks();
}

void Mbc1Cartridge::RomWriteByte(unsigned short address, unsigned char value)
{
	if (address < 0x2000)
	{
		_ramEnabled = (value & 0xf) == 0xa;
	}
	else if (address < 0x4000)
	{
		// Bank 0 is translated to 1 on the 5-bit register only, so on small ROMs some writes can map in bank 0
		_bank1 = std::max(1, value & 0x1f);
	}
	else if (address < 0x6000)
	{
		_bank2 = value & 0x3;
	}
	else
	{
		_mode1 = (value & 0x1) != 0;
	}

	UpdateRomBanks();
}

unsigned char Mbc1Cartridge::RamReadByte(unsigned short address) const
{
	return _ramEnabled ? ReadRamBank(_mode1 ? _bank2 : 0, address) : 0xff;
}

void Mbc1Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	if (_ramEnabled) WriteRamBank(_mode1 ? _bank2 : 0, address, value);
}

void Mbc1Cartridge::WriteRegisters(StateWriter& writer) const
{
	writer.BeginSection("MBC1");
	writer.WriteInt(_bank1);
	writer.WriteInt(_bank2);
	writer.WriteBool(_mode1);
	writer.WriteBool(_ramEnabled);
	writer.EndSection();
}

void Mbc1Cartridge::ReadRegisters(StateReader& reader)
{
	reader.BeginSection("MBC1");
	_bank1 = reader.ReadInt(1, 0x1f);
	_bank2 = reader.ReadInt(0, 3);
	_mode1 = reader.ReadBool();
	_ramEnabled = reader.ReadBool();
	reader.EndSection();
}
