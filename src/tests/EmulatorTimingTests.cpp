#include "stdafx.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "../core/Emulator.h"
#include "../core/GbInternalRom.h"

namespace
{
	const unsigned short LogoInBootRom = 0xa8;
	const unsigned short LogoInHeader = 0x104;
	const unsigned short LogoSize = 48;
	const unsigned short HeaderChecksumStart = 0x134;
	const unsigned short HeaderChecksum = 0x14d;

	// A 32 KB ROM whose header passes the boot ROM's logo and checksum checks, so the boot ROM hands over to it,
	// with the given program at the $100 entry point
	std::shared_ptr<Cartridge> CartridgeWithProgram(const std::vector<unsigned char>& program)
	{
		std::vector<unsigned char> rom(2 * MemoryMap::RomBankSize);
		std::copy(program.begin(), program.end(), rom.begin() + 0x100);

		for (auto i = 0; i < LogoSize; i++) rom[LogoInHeader + i] = GbInternalRom::ReadByte(LogoInBootRom + i);

		// Per https://gbdev.io/pandocs/The_Cartridge_Header.html#014d--header-checksum
		unsigned char checksum = 0;
		for (auto address = HeaderChecksumStart; address < HeaderChecksum; address++) checksum = checksum - rom[address] - 1;
		rom[HeaderChecksum] = checksum;

		return std::make_shared<Cartridge>(std::move(rom), 0);
	}

	// JR -2: spin at the entry point
	const std::vector<unsigned char> SpinProgram{ 0x18, 0xfe };
}

// Pan Docs gives no figure for how long the DMG boot ROM runs, so the expectation comes from its code
// (https://codeberg.org/ISSOtm/gb-bootroms/src/branch/master/src/dmg.asm). It scrolls the logo down one line per
// step for 100 steps, then holds it for 32 more. Each step waits for LY to read 144 ($90) 24 times (2 x 12) in a
// 44-clock loop, and LY reads 144 for only one 456-clock line per frame
// (https://gbdev.io/pandocs/STAT.html#ff44--ly-lcd-y-coordinate-read-only), so a step takes about 2.5 frames and
// the whole boot about 330 frames (5.5 s). PyBoy (https://github.com/Baekalfen/PyBoy), running the same boot ROM,
// hands over after 336 frames
TEST(EmulatorTimingTest, BootRomHandsOverAfterAbout333Frames)
{
	Emulator emulator{ CartridgeWithProgram(SpinProgram) };

	auto frames = 0;
	while (emulator.GetMemoryMap().InternalRomEnabled() && frames < 1000)
	{
		emulator.GetFrame();
		frames++;
	}

	EXPECT_GE(frames, 330);
	EXPECT_LE(frames, 337);
}
