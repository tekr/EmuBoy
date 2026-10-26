#pragma once
#include <vector>
#include "../core/Cartridge.h"
#include "../core/CartridgeFactory.h"

// Helpers for testing battery saves with ROMs whose headers declare the cartridge type and RAM size
namespace SaveFileTestHelpers
{
	// Cartridge type codes, per https://gbdev.io/pandocs/The_Cartridge_Header.html#0147--cartridge-type
	const unsigned char Mbc1Ram = 0x02;
	const unsigned char Mbc1RamBattery = 0x03;
	const unsigned char Mbc2Battery = 0x06;
	const unsigned char Mbc3TimerBattery = 0x0f;
	const unsigned char Mbc3TimerRamBattery = 0x10;
	const unsigned char Mbc5RamBattery = 0x1b;

	// RAM size codes, per https://gbdev.io/pandocs/The_Cartridge_Header.html#0149--ram-size
	const unsigned char NoRam = 0x00;
	const unsigned char Ram8K = 0x02;
	const unsigned char Ram32K = 0x03;
	const unsigned char Ram128K = 0x04;

	const size_t Ram8KSize = 8192;

	inline std::vector<unsigned char> Rom(unsigned char cartridgeType, unsigned char ramSize)
	{
		std::vector<unsigned char> rom(0x8000);
		rom[CartridgeFactory::CartridgeTypeHeaderOffset] = cartridgeType;
		rom[CartridgeFactory::RamSizeHeaderOffset] = ramSize;
		return rom;
	}

	// Fills the RAM with a pattern that differs from byte to byte and bank to bank
	inline void FillRam(Cartridge& cartridge, int seed)
	{
		auto& ram = cartridge.GetRam();
		for (size_t i = 0; i < ram.size(); i++) ram[i] = static_cast<unsigned char>(i * seed + i / Cartridge::RamBankSize);
	}

	inline void EnableRam(Cartridge& cartridge)
	{
		cartridge.RomWriteByte(0x0000, 0x0a);
	}
}
