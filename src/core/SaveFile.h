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
// MBC2's 512 x 4-bit RAM is stored as 512 bytes, as other emulators do, with each upper nibble set as the game reads
// it back; the upper nibbles are ignored on loading.
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
	static std::string Deserialise(Cartridge& cartridge, const std::vector<unsigned char>& data, int64_t unixTime);
};
