#pragma once
#include "Cartridge.h"
#include <cstdint>
#include <string>
#include <vector>

// The battery save (.sav) format shared by most Game Boy emulators, so saves can move between them: a raw dump of
// the cartridge RAM, followed for MBC3 cartridges with a clock by the RTC footer that BGB, VBA-M, mGBA and SameBoy use:
// the five clock registers (seconds, minutes, hours, day low, day high) then the five latched registers, each as a
// 32-bit little-endian value, then the UNIX time of the save as a 64-bit little-endian value. Older VBA versions
// wrote a 32-bit time, giving a 44-byte footer that is accepted on loading. See mGBA's GBMBCRTCSaveBuffer
// (include/mgba/internal/gb/mbc.h) and SameBoy's rtc_save_t (Core/gb.c).
//
// SameBoy and VBA-M pair the time stamp with the clock registers, as Emu Boy does. mGBA instead pairs it with the
// latched registers, storing the time of the last latch and reading only the latched fields back. Saving the current
// clock registers in both sets of fields, stamped with the current time, gives the right time to either; the game
// latches again before reading the clock anyway. Loading an mGBA save takes its clock registers, so the clock runs
// ahead by the time between the game's last latch and mGBA saving.
//
// MBC2's 512 x 4-bit RAM is stored as 512 bytes, as SameBoy and VBA-M do, with each upper nibble set as the game
// reads it back; the upper nibbles are ignored on loading. mGBA packs it into 256 bytes instead, byte i holding
// address 2i in its lower nibble and 2i+1 in its upper one (see _GBMBC2 and _GBMBC2Read in src/gb/mbc/mbc.c), so a
// 256-byte MBC2 save is unpacked on loading.
//
// RAM size code 1 is 2 KB, saved as 2 KB as BGB, SameBoy and VBA-M do. mGBA gives it a whole 8 KB bank and saves that,
// so an 8 KB save for 2 KB RAM loads its first 2 KB without a warning.
class SaveFile
{
public:
	static const size_t RtcFooterSize = 48;
	static const size_t LegacyRtcFooterSize = 44;

	// The save file's size for the cartridge
	static size_t ExpectedSize(Cartridge& cartridge);

	// The cartridge's battery-backed RAM and clock, stamped with the given UNIX time
	static std::vector<unsigned char> Serialise(Cartridge& cartridge, int64_t unixTime);

	// Restores the cartridge's RAM and clock from a save, moving the clock on by the time that passed between the
	// save's time stamp and unixTime. A save of the wrong size is loaded as far as it fits. Returns a warning, or
	// an empty string if the save matched the cartridge
	static std::string Deserialise(Cartridge& cartridge, const std::vector<unsigned char>& save, int64_t unixTime);
};
