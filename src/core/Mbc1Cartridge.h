#pragma once
#include "Cartridge.h"

// MBC1: up to 2 MB ROM and 32 KB RAM, per https://gbdev.io/pandocs/MBC1.html
class Mbc1Cartridge : public Cartridge
{
	// 5-bit BANK1 register: the low bits of the $4000-$7FFF ROM bank
	int _bank1{ 1 };

	// 2-bit BANK2 register: the upper ROM bank bits, or the RAM bank in mode 1
	int _bank2{ 0 };

	// Mode 1 also applies BANK2 to the $0000-$3FFF ROM bank and to the RAM bank
	bool _mode1{ false };
	bool _ramEnabled{ false };

	// Multicarts (MBC1M) wire BANK2 to ROM bank bits 4-5 instead of 5-6, so BANK1's top bit is unused
	int _bank2Shift;

	void UpdateRomBanks();

protected:
	void ResetRegisters() override;
	void WriteRegisters(StateWriter& writer) const override;
	void ReadRegisters(StateReader& reader) override;

public:
	Mbc1Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery = false, bool isMulticart = false);

	// Multicarts hold several 256 KB games, each with its own header and boot logo
	static bool IsMulticart(const std::vector<unsigned char>& rom);

	std::unique_ptr<Cartridge> Clone() const override { return std::make_unique<Mbc1Cartridge>(*this); }

	void RomWriteByte(unsigned short address, unsigned char value) override;

	unsigned char RamReadByte(unsigned short address) const override;
	void RamWriteByte(unsigned short address, unsigned char value) override;
};
