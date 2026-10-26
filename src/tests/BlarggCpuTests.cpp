#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "BlarggRomRunner.h"

// Runs blargg's CPU, timing and HALT test ROMs (see roms/readme.md)

class BlarggCpuInstrsTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggCpuInstrsTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/cpu_instrs/") + GetParam() + ".gb", 60 * 60);
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(CpuInstrs, BlarggCpuInstrsTest, testing::Values(
	"01-special", "02-interrupts", "03-op sp,hl", "04-op r,imm", "05-op rp", "06-ld r,r", "07-jr,jp,call,ret,rst",
	"08-misc instrs", "09-op r,r", "10-bit ops", "11-op a,(hl)"));

class BlarggMemTimingTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggMemTimingTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/") + GetParam() + ".gb", 60 * 20);
}

INSTANTIATE_TEST_CASE_P(MemTiming, BlarggMemTimingTest, testing::Values(
	"mem_timing/01-read_timing", "mem_timing/02-write_timing", "mem_timing/03-modify_timing",
	"mem_timing-2/01-read_timing", "mem_timing-2/02-write_timing", "mem_timing-2/03-modify_timing"));

TEST(BlarggTimingTest, InstrTimingPasses)
{
	ExpectBlarggRomPasses("roms/instr_timing/instr_timing.gb", 60 * 20);
}

TEST(BlarggHaltTest, HaltBugPasses)
{
	ExpectBlarggRomPasses("roms/halt_bug/halt_bug.gb", 60 * 20);
}
