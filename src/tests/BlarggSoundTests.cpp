#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "BlarggRomRunner.h"

// Runs blargg's dmg_sound test ROMs (see roms/readme.md), which report through cartridge RAM at $A000

class BlarggSoundTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggSoundTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/dmg_sound/") + GetParam() + ".gb", 60 * 60);
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(DmgSound, BlarggSoundTest, testing::Values(
	"01-registers", "02-len ctr", "03-trigger", "04-sweep", "05-sweep details", "06-overflow on trigger",
	"07-len sweep period sync", "08-len ctr during power", "09-wave read while on", "10-wave trigger while on",
	"11-regs after power", "12-wave write while on"));
