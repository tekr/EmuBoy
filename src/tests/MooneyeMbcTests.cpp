#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "MooneyeRomRunner.h"

// Runs the mooneye-test-suite's MBC1, MBC2 and MBC5 test ROMs (see roms/readme.md)

class MooneyeMbcTest : public testing::TestWithParam<const char*>
{
};

TEST_P(MooneyeMbcTest, Passes)
{
	SCOPED_TRACE(GetParam());
	auto result = MooneyeRomRunner::Run(std::string("roms/mooneye-test-suite/emulator-only/") + GetParam() + ".gb");

	EXPECT_EQ(MooneyeRomRunner::Outcome::Passed, result.Status) << result;
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(Mbc1, MooneyeMbcTest, testing::Values(
	"mbc1/bits_bank1", "mbc1/bits_bank2", "mbc1/bits_mode", "mbc1/bits_ramg", "mbc1/multicart_rom_8Mb",
	"mbc1/ram_64kb", "mbc1/ram_256kb", "mbc1/rom_512kb", "mbc1/rom_1Mb", "mbc1/rom_2Mb", "mbc1/rom_4Mb",
	"mbc1/rom_8Mb", "mbc1/rom_16Mb"));

INSTANTIATE_TEST_CASE_P(Mbc2, MooneyeMbcTest, testing::Values(
	"mbc2/bits_ramg", "mbc2/bits_romb", "mbc2/bits_unused", "mbc2/ram", "mbc2/rom_512kb", "mbc2/rom_1Mb",
	"mbc2/rom_2Mb"));

INSTANTIATE_TEST_CASE_P(Mbc5, MooneyeMbcTest, testing::Values(
	"mbc5/rom_512kb", "mbc5/rom_1Mb", "mbc5/rom_2Mb", "mbc5/rom_4Mb", "mbc5/rom_8Mb", "mbc5/rom_16Mb",
	"mbc5/rom_32Mb", "mbc5/rom_64Mb"));
