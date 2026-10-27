#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "MooneyeRomRunner.h"

// Runs the mooneye-test-suite acceptance ROMs that apply to the DMG (see roms/readme.md, which lists those left out
// and why). Each must pass

class MooneyeAcceptanceTest : public testing::TestWithParam<const char*>
{
};

TEST_P(MooneyeAcceptanceTest, Passes)
{
	SCOPED_TRACE(GetParam());

	// Each finishes within a few seconds of emulated time after the boot ROM
	auto result = MooneyeRomRunner::Run(std::string("roms/mooneye-test-suite/acceptance/") + GetParam() + ".gb", 60 * 20);

	EXPECT_EQ(MooneyeRomRunner::Outcome::Passed, result.Status) << result;
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(Processor, MooneyeAcceptanceTest, testing::Values(
	"add_sp_e_timing", "call_cc_timing", "call_cc_timing2", "call_timing", "call_timing2", "di_timing-GS",
	"div_timing", "ei_sequence", "ei_timing", "halt_ime0_ei", "halt_ime0_nointr_timing", "halt_ime1_timing",
	"halt_ime1_timing2-GS", "if_ie_registers", "intr_timing", "jp_cc_timing", "jp_timing", "ld_hl_sp_e_timing",
	"pop_timing", "push_timing", "rapid_di_ei", "ret_cc_timing", "ret_timing", "reti_intr_timing", "reti_timing",
	"rst_timing", "instr/daa", "interrupts/ie_push"));

INSTANTIATE_TEST_CASE_P(Bits, MooneyeAcceptanceTest, testing::Values(
	"bits/mem_oam", "bits/reg_f", "bits/unused_hwio-GS"));

INSTANTIATE_TEST_CASE_P(Boot, MooneyeAcceptanceTest, testing::Values(
	"boot_div-dmgABCmgb", "boot_hwio-dmgABCmgb", "boot_regs-dmgABC", "serial/boot_sclk_align-dmgABCmgb"));

INSTANTIATE_TEST_CASE_P(DmaController, MooneyeAcceptanceTest, testing::Values(
	"oam_dma_restart", "oam_dma_start", "oam_dma_timing", "oam_dma/basic", "oam_dma/reg_read",
	"oam_dma/sources-GS"));

INSTANTIATE_TEST_CASE_P(Ppu, MooneyeAcceptanceTest, testing::Values(
	"ppu/hblank_ly_scx_timing-GS", "ppu/intr_1_2_timing-GS", "ppu/intr_2_0_timing",
	"ppu/intr_2_mode0_timing_sprites", "ppu/intr_2_mode0_timing", "ppu/intr_2_mode3_timing",
	"ppu/intr_2_oam_ok_timing", "ppu/lcdon_timing-GS", "ppu/lcdon_write_timing-GS", "ppu/stat_irq_blocking",
	"ppu/stat_lyc_onoff", "ppu/vblank_stat_intr-GS"));

INSTANTIATE_TEST_CASE_P(TimerUnit, MooneyeAcceptanceTest, testing::Values(
	"timer/div_write", "timer/rapid_toggle", "timer/tim00_div_trigger", "timer/tim00", "timer/tim01_div_trigger",
	"timer/tim01", "timer/tim10_div_trigger", "timer/tim10", "timer/tim11_div_trigger", "timer/tim11",
	"timer/tima_reload", "timer/tima_write_reloading", "timer/tma_write_reloading"));
