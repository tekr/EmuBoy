#include "stdafx.h"
#include "Cartridge.h"
#include <cassert>

namespace
{
	size_t RomBankCount(size_t romSize)
	{
		// Real ROMs are a power-of-two number of banks; rounding others up keeps bank masking simple
		size_t banks = 1;
		while (banks * Cartridge::RomBankSize < romSize) banks <<= 1;
		return banks;
	}
}

Cartridge::Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery) : _rom{ std::move(rom) },
	_ram(ramSize), _hasBattery(hasBattery), _ramMask(ramSize == 0 ? 0 : ramSize - 1)
{
	assert((ramSize & (ramSize - 1)) == 0);

	auto banks = RomBankCount(_rom.size());

	// Padding reads as $FF, like an absent ROM chip
	_rom.resize(banks * RomBankSize, 0xff);
	_romBankMask = static_cast<int>(banks - 1);

	SetRomBanks(0, 1);
}

void Cartridge::SetRomBanks(int bank0, int bank1)
{
	_romBank0Offset = (bank0 & _romBankMask) * RomBankSize;
	_romBank1Offset = (bank1 & _romBankMask) * RomBankSize;
}

size_t Cartridge::RamIndex(int bank, unsigned short address) const
{
	// Banks and addresses beyond the RAM's size wrap, as the chip's upper address lines aren't connected. RAM is a
	// power-of-two size, so this mirrors as the hardware does with no division on each access
	return (bank * RamBankSize | (address & (RamBankSize - 1))) & _ramMask;
}

unsigned char Cartridge::ReadRamBank(int bank, unsigned short address) const
{
	return _ram.empty() ? 0xff : _ram[RamIndex(bank, address)];
}

void Cartridge::WriteRamBank(int bank, unsigned short address, unsigned char value)
{
	if (!_ram.empty()) _ram[RamIndex(bank, address)] = value;
}
