#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "BlarggRomRunner.h"

// Runs blargg's oam_bug ROMs (see roms/readme.md), which check the DMG's OAM corruption bug and the LCD timing it
// depends on. 7-timing_effect isn't run: its output (an OAM dump for each of the 20 timings that corrupt) is longer
// than the 8 KB of cartridge RAM at $A000-$BFFF it logs to, so the log runs on past $C000, over its own code, and it
// crashes. In a scratch build that kept its code from being overwritten, it passed its checksum of that output

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
