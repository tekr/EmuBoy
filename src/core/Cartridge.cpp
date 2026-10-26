#include "stdafx.h"
#include "Cartridge.h"
#include "StateStream.h"

namespace
{
	size_t RomBankCount(size_t romSize)
	{
		// Real ROMs are a power-of-two number of banks; rounding others up keeps bank masking simple
		size_t banks = 1;
		while (banks * Cartridge::RomBankSize < romSize) banks <<= 1;
		return banks;
	}
}

Cartridge::Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery) : _rom{ std::move(rom) },
	_ram(ramSize), _hasBattery(hasBattery)
{
	auto banks = RomBankCount(_rom.size());

	// Padding reads as $FF, like an absent ROM chip
	_rom.resize(banks * RomBankSize, 0xff);
	_romBankMask = static_cast<int>(banks - 1);

	SetRomBanks(0, 1);
}

void Cartridge::SetRomBanks(int bank0, int bank1)
{
	_romBank0Offset = (bank0 & _romBankMask) * RomBankSize;
	_romBank1Offset = (bank1 & _romBankMask) * RomBankSize;
}

size_t Cartridge::RamIndex(int bank, unsigned short address) const
{
	auto banks = (_ram.size() + RamBankSize - 1) / RamBankSize;
	return (bank % banks * RamBankSize + (address & (RamBankSize - 1))) % _ram.size();
}

unsigned char Cartridge::ReadRamBank(int bank, unsigned short address) const
{
	return _ram.empty() ? 0xff : _ram[RamIndex(bank, address)];
}

void Cartridge::WriteRamBank(int bank, unsigned short address, unsigned char value)
{
	if (_ram.empty()) return;

	// Games often rewrite RAM with the values it already holds, which needn't be saved
	auto& byte = _ram[RamIndex(bank, address)];
	if (byte == value) return;

	byte = value;
	MarkChanged();
}

void Cartridge::WriteState(StateWriter& writer) const
{
	writer.BeginSection("CART");

	writer.WriteInt(static_cast<int>(_romBank0Offset / RomBankSize));
	writer.WriteInt(static_cast<int>(_romBank1Offset / RomBankSize));
	writer.WriteU32(static_cast<uint32_t>(_ram.size()));
	writer.WriteBytes(_ram.data(), _ram.size());
	WriteRegisters(writer);

	writer.EndSection();
}

void Cartridge::ReadState(StateReader& reader)
{
	reader.BeginSection("CART");

	auto bank0 = reader.ReadInt(0, _romBankMask);
	auto bank1 = reader.ReadInt(0, _romBankMask);
	SetRomBanks(bank0, bank1);

	auto ramSize = reader.ReadU32();
	if (ramSize != _ram.size())
	{
		reader.Fail("it has " + std::to_string(ramSize) + " bytes of cartridge RAM rather than " + std::to_string(_ram.size()));
	}

	reader.ReadBytes(_ram.data(), _ram.size());
	ReadRegisters(reader);

	reader.EndSection();
	MarkChanged();
}
