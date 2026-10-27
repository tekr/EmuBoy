#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "GbMicrotestRunner.h"

// Runs ROMs from aappleby's gbmicrotest (see GbMicrotestRunner.h and roms/readme.md), checked on a DMG. Each must pass

class GbMicrotest : public testing::TestWithParam<const char*>
{
};

TEST_P(GbMicrotest, Passes)
{
	SCOPED_TRACE(GetParam());

	auto result = GbMicrotestRunner::Run(std::string("roms/gbmicrotest/") + GetParam() + ".gb");

	EXPECT_EQ(GbMicrotestRunner::Outcome::Passed, result.Outcome) << result;
}

// The STAT write bug: a write to STAT on the DMG requests a STAT interrupt if, for a moment, any interrupt source
// would be selected by $FF. The tests write $00 to STAT at a machine cycle of lines 0, 1 and 143 of the first frame
// after turning the LCD on, or line 0 of the second (with LYC at 0), and read IF. stat_write_glitch_l154_d is left
// out: it expects IF to read $E0 after running through line 144 without first clearing IF, unlike its twin l154_c,
// so without the V-Blank interrupt. But stat_write_glitch_l143_d, also checked on a DMG, runs through line 144 of the
// same first frame and expects $E3, with the V-Blank interrupt requested, as SameBoy and mGBA request it.
// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(StatWriteBug, GbMicrotest, testing::Values(
	"stat_write_glitch_l0_a", "stat_write_glitch_l0_b", "stat_write_glitch_l0_c", "stat_write_glitch_l1_a",
	"stat_write_glitch_l1_b", "stat_write_glitch_l1_c", "stat_write_glitch_l1_d", "stat_write_glitch_l143_a",
	"stat_write_glitch_l143_b", "stat_write_glitch_l143_c", "stat_write_glitch_l143_d", "stat_write_glitch_l154_a",
	"stat_write_glitch_l154_b", "stat_write_glitch_l154_c"));
