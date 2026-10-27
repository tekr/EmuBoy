#pragma once
#include "Emulator.h"
#include <cstdint>
#include <string>
#include <vector>

// Save states: a snapshot of the whole emulator, to carry on from later. Emu Boy's own format, with every value
// little-endian:
//
//   "EMUBOYSS"     Magic
//   u32            Format version, FormatVersion
//   ROM section    The game the state belongs to: the title bytes from its header ($0134-$0143, for messages), its
//                  size and the CRC-32 of the whole ROM
//   Sections       Each component's state, as Emulator::WriteState writes it: CPU, memory, PPU, OAM DMA, timer,
//                  APU, serial port, joypad and cartridge (mapper, RAM and clock)
//   u32            CRC-32 of everything before it
//
// A section is a four-character tag, its length as a u32 and its contents. A state from another version of the
// format is rejected rather than converted, as is one from another game.
class SaveState
{
public:
	static const uint32_t FormatVersion = 2;

	static std::vector<unsigned char> Create(const Emulator& emulator);

	// Restores the emulator to a state. Returns an empty string, or why the state can't be loaded, in which case the
	// emulator is left as it was: the state is checked in full, by loading it into a copy, before it is applied
	static std::string Restore(Emulator& emulator, const std::vector<unsigned char>& state);
};
