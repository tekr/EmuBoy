#pragma once
#include "Cartridge.h"

// MBC2: up to 256 KB ROM and built-in 512 x 4-bit RAM, per https://gbdev.io/pandocs/MBC2.html
class Mbc2Cartridge : public Cartridge
{
	bool _ramEnabled{ false };

protected:
	void ResetRegisters() override { _ramEnabled = false; }
	void WriteRegisters(StateWriter& writer) const override;
	void ReadRegisters(StateReader& reader) override;

public:
	static const size_t RamSize = 512;

	Mbc2Cartridge(std::vector<unsigned char>&& rom, bool hasBattery = false);

	std::unique_ptr<Cartridge> Clone() const override { return std::make_unique<Mbc2Cartridge>(*this); }

	void RomWriteByte(unsigned short address, unsigned char value) override;

	unsigned char RamReadByte(unsigned short address) const override;
	void RamWriteByte(unsigned short address, unsigned char value) override;
};
