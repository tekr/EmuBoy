#pragma once
#include "Cartridge.h"
#include "RealTimeClock.h"

// MBC3: up to 2 MB ROM, 32 KB RAM and an optional real-time clock, per https://gbdev.io/pandocs/MBC3.html
class Mbc3Cartridge : public Cartridge
{
	static const int FirstClockRegister = 0x08;

	// $00-$03 selects a RAM bank, $08-$0C a clock register
	int _ramBankOrClockRegister{ 0 };
	bool _ramAndClockEnabled{ false };

	bool _hasClock;
	RealTimeClock _clock;

	// Index of the selected clock register, or -1 if a RAM bank (or nothing) is selected
	int SelectedClockRegister() const;

public:
	Mbc3Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery = false, bool hasClock = false);

	void RomWriteByte(unsigned short address, unsigned char value) override;

	unsigned char RamReadByte(unsigned short address) const override;
	void RamWriteByte(unsigned short address, unsigned char value) override;

	void RunCycles(int cycles) override;
	RealTimeClock* GetRealTimeClock() override { return _hasClock ? &_clock : nullptr; }
};
