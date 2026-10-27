#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class RealTimeClock;
class StateWriter;
class StateReader;

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
			? _romData[_romBank0Offset + address]
			: _romData[_romBank1Offset + (address & (RomBankSize - 1))];
	}

	// Writes to $0000-$7FFF set the mapper's registers
	virtual void RomWriteByte(unsigned short address, unsigned char value) = 0;

	// Address is relative to the start of cartridge RAM at $A000
	virtual unsigned char RamReadByte(unsigned short address) const = 0;
	virtual void RamWriteByte(unsigned short address, unsigned char value) = 0;

	// Returns the mapper's registers to their power-on state, as when the Game Boy is switched off and on again with
	// the cartridge in. RAM and any real-time clock keep their contents, as the battery (if any) keeps them powered
	void Reset()
	{
		SetRomBanks(0, 1);
		ResetRegisters();
	}

	// Advances any real-time hardware on the cartridge by emulated clock cycles
	virtual void RunCycles(int cycles) {}

	// The cartridge's real-time clock, or null if it hasn't one
	virtual RealTimeClock* GetRealTimeClock() { return nullptr; }

	// Whether a battery keeps the RAM (and clock, if any) contents while the Game Boy is off
	bool HasBattery() const { return _hasBattery; }

	// The RAM contents, for saving and restoring battery-backed RAM
	std::vector<unsigned char>& GetRam() { return _ram; }
	const std::vector<unsigned char>& GetRam() const { return _ram; }

	// Whether the RAM or clock registers have changed since MarkSaved, so the front end only saves when needed
	bool HasUnsavedChanges() const { return _hasUnsavedChanges; }
	void MarkSaved() { _hasUnsavedChanges = false; }

	// The ROM, padded to a whole number of banks
	const std::vector<unsigned char>& GetRom() const { return *_rom; }

	// The CRC-32 of the whole ROM (as padded), which identifies the game. Worked out the first time it's needed
	uint32_t GetRomCrc() const;

	// A copy of the cartridge in its current state, e.g. to try loading a save state into. It shares the ROM, which
	// doesn't change, so copying is quick however big the ROM
	virtual std::unique_ptr<Cartridge> Clone() const = 0;

	// Save states: the mapper's registers, the RAM and any clock, but not the ROM, which a state is tied to. Loading
	// a state counts as changing the RAM, so the battery save is brought into line with it at the next save
	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);

protected:
	// Shared by clones
	std::shared_ptr<std::vector<unsigned char>> _rom;
	std::vector<unsigned char> _ram;

	// ramSize must be 0 or a power of two (as every size the cartridge header can declare is), as RAM addresses are
	// masked to it
	Cartridge(std::vector<unsigned char>&& rom, size_t ramSize, bool hasBattery);

	// Maps ROM banks into $0000-$3FFF and $4000-$7FFF. Banks wrap to the ROM's size, as the mapper's upper bank
	// lines aren't connected on smaller ROMs
	void SetRomBanks(int bank0, int bank1);

	// Reads and writes a RAM bank, wrapping banks to the RAM's size. Without RAM, reads give $FF (open bus) and
	// writes are ignored
	unsigned char ReadRamBank(int bank, unsigned short address) const;
	void WriteRamBank(int bank, unsigned short address, unsigned char value);

	void MarkChanged() { _hasUnsavedChanges = true; }

	// Called by Reset after the ROM banks are back at 0 and 1
	virtual void ResetRegisters() {}

	// The mapper's own registers in save states. The ROM banks mapped in are saved for all mappers
	virtual void WriteRegisters(StateWriter& writer) const {}
	virtual void ReadRegisters(StateReader& reader) {}

private:
	bool _hasBattery;
	bool _hasUnsavedChanges{ false };
	size_t _ramMask;
	int _romBankMask;

	// _rom's bytes, read on every instruction fetch
	const unsigned char* _romData;

	mutable uint32_t _romCrc{ 0 };
	mutable bool _romCrcKnown{ false };
	size_t _romBank0Offset{ 0 };
	size_t _romBank1Offset{ 0 };

	size_t RamIndex(int bank, unsigned short address) const;
};
