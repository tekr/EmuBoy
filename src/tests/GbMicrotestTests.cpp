#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// Runs ROMs from aappleby's gbmicrotest (https://github.com/aappleby/gbmicrotest; see roms/readme.md), checked on a
// DMG. Each tests one register at one machine cycle, then leaves the value it read at $FF80, the value expected at
// $FF81, and $01 at $FF82 if they match or $FF if they don't

struct MicrotestResult
{
	bool RomFound;
	unsigned char Actual;
	unsigned char Expected;
	unsigned char Verdict;
};

static MicrotestResult RunMicrotest(const std::string& romPath)
{
	auto cartridge = CartridgeFactory::LoadFromFile(romPath);
	if (cartridge == nullptr) return { false, 0, 0, 0 };

	Emulator emulator{ cartridge };
	auto& memoryMap = emulator.GetMemoryMap();

	// The boot ROM takes around 333 frames (see EmulatorTimingTests); the tests finish within a few after it
	for (auto frame = 0; frame < 400; frame++)
	{
		emulator.GetFrame();

		auto verdict = memoryMap.ReadByte(0xff82);
		if (verdict == 0x01 || verdict == 0xff) return { true, memoryMap.ReadByte(0xff80), memoryMap.ReadByte(0xff81), verdict };
	}

	return { true, memoryMap.ReadByte(0xff80), memoryMap.ReadByte(0xff81), 0 };
}

class GbMicrotest : public testing::TestWithParam<const char*>
{
};

TEST_P(GbMicrotest, Passes)
{
	SCOPED_TRACE(GetParam());

	auto result = RunMicrotest(std::string("roms/gbmicrotest/") + GetParam() + ".gb");

	ASSERT_TRUE(result.RomFound) << "ROM not found (run the tests from src/tests)";
	EXPECT_EQ(0x01, result.Verdict) << std::hex << "read $" << +result.Actual << ", expected $" << +result.Expected;
}

// The STAT write bug: a write to STAT on the DMG requests a STAT interrupt if, for a moment, any interrupt source
// would be selected by $FF. The tests write $00 to STAT at a machine cycle of lines 0, 1, 143 and 153 (with LYC at 0)
// and read IF. stat_write_glitch_l154_d is left out: it expects IF to read $E0 after running through line 144, which
// requests the V-Blank interrupt, without clearing IF as its twin l154_c does.
// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(StatWriteBug, GbMicrotest, testing::Values(
	"stat_write_glitch_l0_a", "stat_write_glitch_l0_b", "stat_write_glitch_l0_c", "stat_write_glitch_l1_a",
	"stat_write_glitch_l1_b", "stat_write_glitch_l1_c", "stat_write_glitch_l1_d", "stat_write_glitch_l143_a",
	"stat_write_glitch_l143_b", "stat_write_glitch_l143_c", "stat_write_glitch_l143_d", "stat_write_glitch_l154_a",
	"stat_write_glitch_l154_b", "stat_write_glitch_l154_c"));
