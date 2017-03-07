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

unsigned char Cartridge::RamReadByte(unsigned short address) const
{
	return _ramEnabled ? _ram[address + _selectedRamBank * MemoryMap::RamBankSize] : 0x0;
}

void Cartridge::RomWriteByte(unsigned short address, unsigned char value)
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

void Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	if (_ramEnabled)
	{
		_ram[address + _selectedRamBank * MemoryMap::RamBankSize] = value;
	}
}
