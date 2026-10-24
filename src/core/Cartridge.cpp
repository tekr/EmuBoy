#include "stdafx.h"
#include "Cartridge.h"
#include "MemoryMap.h"
#include <algorithm>
#include <cassert>

Cartridge::Cartridge(std::vector<unsigned char>&& rom, int ramBanks) : _rom{ std::move(rom) },
			_ram(ramBanks * MemoryMap::RamBankSize), _ramEnabled(false)
{
}

unsigned char Cartridge::RomReadByte(unsigned short address) const
{
	auto bank = address < MemoryMap::RomBankSize ? 0 : _selectedRomBank;
	auto index = (address & MemoryMap::RomBankSize - 1) + bank * MemoryMap::RomBankSize;

	assert(index < _rom.size());
	return _rom[index];
}

size_t Cartridge::RamIndex(unsigned short address) const
{
	return address + _selectedRamBank * MemoryMap::RamBankSize;
}

unsigned char Cartridge::RamReadByte(unsigned short address) const
{
	if (!_ramEnabled) return 0x0;

	// Reads from RAM the cartridge doesn't have return 0xff, as on hardware
	auto index = RamIndex(address);
	return index < _ram.size() ? _ram[index] : 0xff;
}

void Cartridge::RomWriteByte(unsigned short address, unsigned char value)
{
	if (address < 0x2000)
	{
		_ramEnabled = (value & 0xf) == 0xa;
	}
	else if (address >= 0x2000 && address < 0x4000)
	{
		_selectedRomBank = (_selectedRomBank & 0x60) | std::max(1, value & 0x1f);
	}
	else if (address < 0x6000)
	{
		if (_mode == Mode::FourMbRom)
		{
			_selectedRamBank = value & 0x3;
		}
		else
		{
			_selectedRomBank = (_selectedRomBank & 0x1f) | (value & 0x3) << 5;
		}
	}
	else
	{
		_mode = value & 0x1 ? Mode::FourMbRom : Mode::SixteenMbRom;
	}
}

void Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	auto index = RamIndex(address);

	if (_ramEnabled && index < _ram.size())
	{
		_ram[index] = value;
	}
}
