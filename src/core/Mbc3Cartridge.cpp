#include "stdafx.h"
#include "Mbc3Cartridge.h"
#include <algorithm>

namespace
{
	const int RamBankCount = 4;
}

Mbc3Cartridge::Mbc3Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery, bool hasClock) :
	Cartridge(std::move(rom), ramSize, hasBattery), _hasClock(hasClock)
{
}

int Mbc3Cartridge::SelectedClockRegister() const
{
	auto reg = _ramBankOrClockRegister - FirstClockRegister;
	return _hasClock && reg >= 0 && reg < RealTimeClock::RegisterCount ? reg : -1;
}

void Mbc3Cartridge::RomWriteByte(unsigned short address, unsigned char value)
{
	if (address < 0x2000)
	{
		_ramAndClockEnabled = (value & 0xf) == 0xa;
	}
	else if (address < 0x4000)
	{
		SetRomBanks(0, std::max(1, value & 0x7f));
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

	if (_ramBankOrClockRegister < RamBankCount) return ReadRamBank(_ramBankOrClockRegister, address);

	// Pan Docs doesn't define other values; they read as open bus
	auto reg = SelectedClockRegister();
	return reg >= 0 ? _clock.ReadRegister(reg) : 0xff;
}

void Mbc3Cartridge::RamWriteByte(unsigned short address, unsigned char value)
{
	if (!_ramAndClockEnabled) return;

	if (_ramBankOrClockRegister < RamBankCount)
	{
		WriteRamBank(_ramBankOrClockRegister, address, value);
	}
	else
	{
		auto reg = SelectedClockRegister();
		if (reg >= 0) _clock.WriteRegister(reg, value);
	}
}

void Mbc3Cartridge::RunCycles(int cycles)
{
	if (_hasClock) _clock.RunCycles(cycles);
}
