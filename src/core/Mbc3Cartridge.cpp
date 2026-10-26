#include "stdafx.h"
#include "Mbc3Cartridge.h"
#include <algorithm>

namespace
{
	const int RamBankSelectCount = 8;
	const size_t Mbc3MaxRomSize = 2 << 20;
	const size_t Mbc3MaxRamSize = 32 << 10;
}

Mbc3Cartridge::Mbc3Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery, bool hasClock,
	bool isMbc30) :
	Cartridge(std::move(rom), ramSize, hasBattery), _hasClock(hasClock), _romBankMask(isMbc30 ? 0xff : 0x7f),
	_ramBankMask(isMbc30 ? 0x7 : 0x3)
{
}

bool Mbc3Cartridge::IsMbc30(size_t romSize, size_t ramSize)
{
	// As SameBoy and Gambatte detect it
	return romSize > Mbc3MaxRomSize || ramSize > Mbc3MaxRamSize;
}

int Mbc3Cartridge::SelectedClockRegister() const
{
	auto reg = _ramBankOrClockRegister - FirstClockRegister;
	return _hasClock && reg >= 0 && reg < RealTimeClock::RegisterCount ? reg : -1;
}

void Mbc3Cartridge::ResetRegisters()
{
	_ramBankOrClockRegister = 0;
	_ramAndClockEnabled = false;
}

void Mbc3Cartridge::RomWriteByte(unsigned short address, unsigned char value)
{
	if (address < 0x2000)
	{
		_ramAndClockEnabled = (value & 0xf) == 0xa;
	}
	else if (address < 0x4000)
	{
		SetRomBanks(0, std::max(1, value & _romBankMask));
	}
	else if (address < 0x6000)
	{
		_ramBankOrClockRegister = value;
	}
	else if (_hasClock)
	{
		_clock.WriteLatch(value);
	}
}

unsigned char Mbc3Cartridge::RamReadByte(unsigned short address) const
{
	if (!_ramAndClockEnabled) return 0xff;

	// Without the MBC30's extra bank line, banks 4-7 mirror 0-3
	if (_ramBankOrClockRegister < RamBankSelectCount) return ReadRamBank(_ramBankOrClockRegister & _ramBankMask, address);

	// Pan Docs doesn't define other values; they read as open bus
	auto reg = SelectedClockRegister();
	return reg >= 0 ? _clock.ReadRegister(reg) : 0xff;
}

void Mbc3Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	if (!_ramAndClockEnabled) return;

	if (_ramBankOrClockRegister < RamBankSelectCount)
	{
		WriteRamBank(_ramBankOrClockRegister & _ramBankMask, address, value);
	}
	else
	{
		auto reg = SelectedClockRegister();
		if (reg < 0) return;

		_clock.WriteRegister(reg, value);
		MarkChanged();
	}
}

void Mbc3Cartridge::RunCycles(int cycles)
{
	if (_hasClock) _clock.RunCycles(cycles);
}
