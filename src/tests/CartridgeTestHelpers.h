#pragma once
#include <vector>
#include "../core/Cartridge.h"

// Helpers for testing mappers with synthetic ROM images whose bytes identify their bank
namespace CartridgeTestHelpers
{
	// Each bank's first two bytes hold its bank number (low byte first); the rest hold the low byte
	inline std::vector<unsigned char> BankNumberedRom(int banks)
	{
		std::vector<unsigned char> rom(banks * Cartridge::RomBankSize);

		for (auto bank = 0; bank < banks; bank++)
		{
			auto start = rom.begin() + bank * Cartridge::RomBankSize;
			std::fill(start, start + Cartridge::RomBankSize, static_cast<unsigned char>(bank));
			start[1] = static_cast<unsigned char>(bank >> 8);
		}

		return rom;
	}

	// The bank mapped at $0000-$3FFF
	inline int Bank0(const Cartridge& cartridge)
	{
		return cartridge.RomReadByte(0x0000) | cartridge.RomReadByte(0x0001) << 8;
	}

	// The bank mapped at $4000-$7FFF
	inline int Bank1(const Cartridge& cartridge)
	{
		return cartridge.RomReadByte(0x4000) | cartridge.RomReadByte(0x4001) << 8;
	}
}
