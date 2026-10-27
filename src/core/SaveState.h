#pragma once
#include "Emulator.h"
#include "StateStream.h"
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
//   Sections       As Emulator::WriteState writes them: the model (DMG or CGB), then each component's state: CPU,
//                  memory, PPU, OAM DMA, timer, APU, serial port, joypad and cartridge (mapper, RAM and clock), then
//                  in CGB states the CGB's VRAM DMA
//   u32            CRC-32 of everything before it
//
// A section is a four-character tag, its length as a u32 and its contents. Version 3 added the Game Boy Color: the
// model section and, in CGB states only, the CGB's state at the end of the components' sections and the VRAM DMA
// section, so a DMG state is a version 2 state with the model section added. Version 2 states, from before then, still
// load, on the DMG. Other versions are rejected, as is a state from another game.
class SaveState
{
public:
	static const uint32_t FormatVersion = StateFormatVersion;
	static const uint32_t OldestFormatVersion = 2;

	static std::vector<unsigned char> Create(const Emulator& emulator);

	// Restores the emulator to a state. Returns an empty string, or why the state can't be loaded, in which case the
	// emulator is left as it was: the state is checked in full, by loading it into a copy, before it is applied
	static std::string Restore(Emulator& emulator, const std::vector<unsigned char>& state);
};
