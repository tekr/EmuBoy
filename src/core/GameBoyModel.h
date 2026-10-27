#pragma once
#include <vector>

// The Game Boy being emulated. A game whose header asks for the Game Boy Color's features runs on a Game Boy Color
// (CGB) in CGB mode; every other game runs on the original Game Boy (DMG), as Emu Boy has always emulated it
enum class GameBoyModel
{
	Dmg,
	Cgb
};

// The model a ROM runs on: CGB if its header's CGB flag ($0143) is $80 (works on both, enhanced in colour) or $C0
// (works only on the CGB), otherwise DMG (https://gbdev.io/pandocs/The_Cartridge_Header.html#0143--cgb-flag). On a
// real CGB other values with bit 7 set also select CGB mode, or with bits 2 or 3 set the DMG compatibility or "PGB"
// modes, as the boot ROM writes the flag to KEY0. No game is known to use them, so they're deliberately run on the DMG
// like every other value, as issue #54 specifies
GameBoyModel ModelForRom(const std::vector<unsigned char>& rom);
