#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "BlarggRomRunner.h"

// Runs blargg's test ROMs on the Game Boy Color in CGB mode (see roms/readme.md): cgb_sound and interrupt_time, which
// are for the CGB, and the CPU tests, which mark themselves as working on both models and run here on the CGB as
// well as (in BlarggCpuTests.cpp) on the DMG

class BlarggCgbSoundTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggCgbSoundTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/cgb_sound/") + GetParam() + ".gb", 60 * 60, nullptr, GameBoyModel::Cgb);
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(CgbSound, BlarggCgbSoundTest, testing::Values(
	"01-registers", "02-len ctr", "03-trigger", "04-sweep", "05-sweep details", "06-overflow on trigger",
	"07-len sweep period sync", "08-len ctr during power", "09-wave read while on", "10-wave trigger while on",
	"11-regs after power", "12-wave"));

class BlarggCgbCpuInstrsTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggCgbCpuInstrsTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/cpu_instrs/") + GetParam() + ".gb", 60 * 60, nullptr, GameBoyModel::Cgb);
}

INSTANTIATE_TEST_CASE_P(CgbCpuInstrs, BlarggCgbCpuInstrsTest, testing::Values(
	"01-special", "02-interrupts", "03-op sp,hl", "04-op r,imm", "05-op rp", "06-ld r,r", "07-jr,jp,call,ret,rst",
	"08-misc instrs", "09-op r,r", "10-bit ops", "11-op a,(hl)"));

class BlarggCgbTimingTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggCgbTimingTest, Passes)
{
	ExpectBlarggRomPasses(std::string("roms/") + GetParam() + ".gb", 60 * 20, nullptr, GameBoyModel::Cgb);
}

INSTANTIATE_TEST_CASE_P(CgbTiming, BlarggCgbTimingTest, testing::Values(
	"instr_timing/instr_timing", "mem_timing/01-read_timing", "mem_timing/02-write_timing",
	"mem_timing/03-modify_timing", "mem_timing-2/01-read_timing", "mem_timing-2/02-write_timing",
	"mem_timing-2/03-modify_timing", "halt_bug/halt_bug"));

// Times interrupts at both CPU speeds, so checks double speed against the timer
TEST(BlarggCgbInterruptTimeTest, Passes)
{
	ExpectBlarggRomPasses("roms/interrupt_time/interrupt_time.gb", 60 * 20, nullptr, GameBoyModel::Cgb);
}
