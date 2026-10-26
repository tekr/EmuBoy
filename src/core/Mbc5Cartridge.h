#pragma once
#include "Cartridge.h"

// MBC5: up to 8 MB ROM and 128 KB RAM, per https://gbdev.io/pandocs/MBC5.html
class Mbc5Cartridge : public Cartridge
{
	// 9-bit ROM bank for $4000-$7FFF. Unlike the other MBCs, bank 0 can be selected
	int _romBank{ 1 };
	int _ramBank{ 0 };
	bool _ramEnabled{ false };
	bool _hasRumble;
	bool _rumbling{ false };

protected:
	void ResetRegisters() override;

public:
	Mbc5Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery = false, bool hasRumble = false);

	// On rumble cartridges, bit 3 of the RAM bank register drives the motor
	bool IsRumbling() const { return _rumbling; }

	void RomWriteByte(unsigned short address, unsigned char value) override;

	unsigned char RamReadByte(unsigned short address) const override;
	void RamWriteByte(unsigned short address, unsigned char value) override;
};
