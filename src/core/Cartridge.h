#pragma once
#include <vector>

class RealTimeClock;

// A game cartridge: its ROM, optional RAM and the memory bank controller (MBC) that maps them into the Game Boy's
// address space. Each mapper derives from this and decodes its own register writes; see
// https://gbdev.io/pandocs/MBCs.html
class Cartridge
{
public:
	static const size_t RomBankSize = 1 << 14;
	static const size_t RamBankSize = 1 << 13;

	virtual ~Cartridge() = default;

	// Reads $0000-$7FFF. Not virtual as it serves every instruction fetch: mappers select banks with SetRomBanks
	unsigned char RomReadByte(unsigned short address) const
	{
		return address < RomBankSize
			? _rom[_romBank0Offset + address]
			: _rom[_romBank1Offset + (address & (RomBankSize - 1))];
	}

	// Writes to $0000-$7FFF set the mapper's registers
	virtual void RomWriteByte(unsigned short address, unsigned char value) = 0;

	// Address is relative to the start of cartridge RAM at $A000
	virtual unsigned char RamReadByte(unsigned short address) const = 0;
	virtual void RamWriteByte(unsigned short address, unsigned char value) = 0;

	// Advances any real-time hardware on the cartridge by emulated clock cycles
	virtual void RunCycles(int cycles) {}

	// The cartridge's real-time clock, or null if it hasn't one
	virtual RealTimeClock* GetRealTimeClock() { return nullptr; }

	// Whether a battery keeps the RAM (and clock, if any) contents while the Game Boy is off
	bool HasBattery() const { return _hasBattery; }

	// The RAM contents, for saving and restoring battery-backed RAM
	std::vector<unsigned char>& GetRam() { return _ram; }
	const std::vector<unsigned char>& GetRam() const { return _ram; }

protected:
	std::vector<unsigned char> _rom;
	std::vector<unsigned char> _ram;

	Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery);

	// Maps ROM banks into $0000-$3FFF and $4000-$7FFF. Banks wrap to the ROM's size, as the mapper's upper bank
	// lines aren't connected on smaller ROMs
	void SetRomBanks(int bank0, int bank1);

	// Reads and writes a RAM bank, wrapping banks to the RAM's size. Without RAM, reads give $FF (open bus) and
	// writes are ignored
	unsigned char ReadRamBank(int bank, unsigned short address) const;
	void WriteRamBank(int bank, unsigned short address, unsigned char value);

private:
	bool _hasBattery;
	int _romBankMask;
	size_t _romBank0Offset{ 0 };
	size_t _romBank1Offset{ 0 };

	size_t RamIndex(int bank, unsigned short address) const;
};
