#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "BlarggRomRunner.h"
#include "../core/MemoryMap.h"

// Runs blargg's oam_bug ROMs (see roms/readme.md), which check the DMG's OAM corruption bug and the LCD timing it
// depends on

namespace
{
	// The ROMs' shell keeps its pointer into the text log at $D883 (text_out_addr: the third variable from bss_base
	// $D880, after testing.s's 1-byte test_code and 2-byte test_name, in common/shell.s of
	// https://github.com/retrio/gb-test-roms/tree/master/oam_bug/source)
	const unsigned short TextOutAddress = 0xd883;
	const unsigned short TextOutBase = 0xa004;

	// Well short of the end of cartridge RAM: the log grows by a few dozen bytes a frame at most
	const unsigned short TextOutLimit = 0xbe00;
}

class BlarggOamBugTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggOamBugTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/oam_bug/") + GetParam() + ".gb", 60 * 20);
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(OamBug, BlarggOamBugTest, testing::Values(
	"1-lcd_sync", "2-causes", "3-non_causes", "4-scanline_timing", "5-timing_bug", "6-timing_no_bug",
	"8-instr_effect"));

TEST(BlarggOamBugTimingEffectTest, Passes)
{
	// 7-timing_effect logs an OAM dump for each of the 20 timings that corrupt, over 8 KB, more than the cartridge
	// RAM at $A000-$BFFF holds, so its log would run on into its own code at $C000 and crash it. Moving the log
	// pointer back to the start of cartridge RAM between frames keeps it within it; the ROM checks its own output
	// with a checksum of the text it prints, so this doesn't affect its result
	ExpectBlarggRomPasses("roms/oam_bug/7-timing_effect.gb", 60 * 60, [](MemoryMap& memory)
	{
		auto textOut = memory.ReadByte(TextOutAddress) | memory.ReadByte(TextOutAddress + 1) << 8;

		if (textOut >= TextOutLimit && textOut < MemoryMap::RamFixed)
		{
			memory.WriteByte(TextOutAddress, TextOutBase & 0xff);
			memory.WriteByte(TextOutAddress + 1, TextOutBase >> 8);
		}
	});
}
