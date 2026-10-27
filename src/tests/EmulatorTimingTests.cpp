#include "stdafx.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "../core/Emulator.h"
#include "../core/GbInternalRom.h"
#include "../core/RomOnlyCartridge.h"

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

		return std::make_shared<RomOnlyCartridge>(std::move(rom), 0);
	}

	// JR -2: spin at the entry point
	const std::vector<unsigned char> SpinProgram{ 0x18, 0xfe };

	// PUSH BC, POP BC, JR -4: a 40-clock loop, which doesn't divide the 70224 clocks of a frame, so instructions
	// regularly straddle the end of a frame. It never halts, so the CPU's clock count is the emulated time
	const std::vector<unsigned char> StraddlingProgram{ 0xc5, 0xc1, 0x18, 0xfc };

	// The most a frame's last instruction can run past the frame's end. A step of the CPU takes at most 24 clocks
	// (an instruction, or an interrupt dispatch) and starts at least 4 clocks before the end, as every clock count
	// and scanline phase is a multiple of 4
	const uint64_t MaxOvershootClocks = 20;
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

TEST(EmulatorTimingTest, EachFrameIsFrameClocksLong)
{
	Emulator emulator{ CartridgeWithProgram(StraddlingProgram) };

	// Through the boot ROM and on into the cartridge's program. Clocks run past the end of one frame come off the
	// next, so after any number of frames only the last frame's overshoot is outstanding
	for (uint64_t frames = 1; frames <= 400; frames++)
	{
		emulator.GetFrame();

		ASSERT_GE(emulator.GetCpuClocks(), frames * Graphics::FrameClocks) << "after " << frames << " frames";
		ASSERT_LE(emulator.GetCpuClocks() - frames * Graphics::FrameClocks, MaxOvershootClocks) << "after " << frames << " frames";
	}
}
