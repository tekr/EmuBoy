#include "stdafx.h"
#include "Cartridge.h"
#include "MemoryMap.h"
#include <algorithm>
#include <cassert>

Cartridge::Cartridge(std::vector<uint8_t>&& rom, int ramBanks) : _rom{ std::move(rom) },
			_ram(ramBanks * MemoryMap::RamBankSize), _ramEnabled(false)
{
}

uint8_t Cartridge::RomReadByte(uint16_t address) const
{
	auto bank = address < MemoryMap::RomBankSize ? 0 : _selectedRomBank;
	auto index = (address & MemoryMap::RomBankSize - 1) + bank * MemoryMap::RomBankSize;

	assert(index < _rom.size());
	return _rom[index];
}

uint8_t Cartridge::RamReadByte(uint16_t address) const
{
	return _ramEnabled ? _ram[address + _selectedRamBank * MemoryMap::RamBankSize] : 0x0;
}

void Cartridge::RomWriteByte(uint16_t address, uint8_t value)
{
	if (address < 0x2000)
	{
		_ramEnabled = (value & 0xa) == 0xa;
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

void Cartridge::RamWriteByte(uint16_t address, uint8_t value)
{
	if (_ramEnabled)
	{
		_ram[address + _selectedRamBank * MemoryMap::RamBankSize] = value;
	}
}
