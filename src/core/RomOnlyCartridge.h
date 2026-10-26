#pragma once
#include "Cartridge.h"

// A 32 KB cartridge without a memory bank controller, optionally with up to 8 KB of RAM that is always accessible
class RomOnlyCartridge : public Cartridge
{
public:
	RomOnlyCartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery = false) :
		Cartridge(std::move(rom), ramSize, hasBattery)
	{
	}

	void RomWriteByte(unsigned short address, unsigned char value) override {}

	unsigned char RamReadByte(unsigned short address) const override { return ReadRamBank(0, address); }
	void RamWriteByte(unsigned short address, unsigned char value) override { WriteRamBank(0, address, value); }
};
