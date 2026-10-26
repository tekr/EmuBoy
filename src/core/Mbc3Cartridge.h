#pragma once
#include "Cartridge.h"
#include "RealTimeClock.h"

// MBC3: up to 2 MB ROM, 32 KB RAM and an optional real-time clock, per https://gbdev.io/pandocs/MBC3.html. The
// MBC30 variant (used by Pocket Monsters Crystal) extends this to 4 MB ROM and 64 KB RAM.
class Mbc3Cartridge : public Cartridge
{
	static const int FirstClockRegister = 0x08;

	// $00-$07 selects a RAM bank, $08-$0C a clock register
	int _ramBankOrClockRegister{ 0 };
	bool _ramAndClockEnabled{ false };

	bool _hasClock;
	RealTimeClock _clock;

	// The MBC30 has one more ROM and RAM bank line
	int _romBankMask;
	int _ramBankMask;

	// Index of the selected clock register, or -1 if a RAM bank (or nothing) is selected
	int SelectedClockRegister() const;

protected:
	// The clock chip is separate from the MBC and runs on the battery, so it keeps counting
	void ResetRegisters() override;

public:
	Mbc3Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery = false, bool hasClock = false,
		bool isMbc30 = false);

	// The header doesn't distinguish the MBC30, so it is recognised by needing more ROM or RAM than the MBC3 supports
	static bool IsMbc30(size_t romSize, size_t ramSize);

	void RomWriteByte(unsigned short address, unsigned char value) override;

	unsigned char RamReadByte(unsigned short address) const override;
	void RamWriteByte(unsigned short address, unsigned char value) override;

	void RunCycles(int cycles) override;
	RealTimeClock* GetRealTimeClock() override { return _hasClock ? &_clock : nullptr; }
};
