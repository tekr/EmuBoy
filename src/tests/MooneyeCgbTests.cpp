#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "MooneyeRomRunner.h"

// Runs mooneye-test-suite ROMs on the Game Boy Color in CGB mode (see roms/readme.md). Their headers ask for the DMG
// ($00 at $0143), so the CGB is chosen here. On a real CGB they'd run in its DMG compatibility mode, which Emu Boy
// doesn't emulate; the CPU, timer, OAM DMA and PPU timings they check are the same in both of the CGB's modes.

class MooneyeCgbTest : public testing::TestWithParam<const char*>
{
};

TEST_P(MooneyeCgbTest, Passes)
{
	SCOPED_TRACE(GetParam());
	auto result = MooneyeRomRunner::Run(std::string("roms/mooneye-test-suite/") + GetParam() + ".gb", 60 * 20, GameBoyModel::Cgb);
	EXPECT_EQ(MooneyeRomRunner::Outcome::Passed, result.Status) << result;
}

// The CGB requests the STAT interrupt for mode 2 at line 144 a machine cycle before V-Blank's
INSTANTIATE_TEST_CASE_P(CgbOnly, MooneyeCgbTest, testing::Values("misc/ppu/vblank_stat_intr-C"));

// The acceptance ROMs Gekkio verified as passing on the CGB
// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(Processor, MooneyeCgbTest, testing::Values(
	"acceptance/add_sp_e_timing", "acceptance/call_cc_timing", "acceptance/call_cc_timing2", "acceptance/call_timing",
	"acceptance/call_timing2", "acceptance/div_timing", "acceptance/ei_sequence", "acceptance/ei_timing",
	"acceptance/halt_ime0_ei", "acceptance/halt_ime0_nointr_timing", "acceptance/halt_ime1_timing",
	"acceptance/if_ie_registers", "acceptance/intr_timing", "acceptance/jp_cc_timing", "acceptance/jp_timing",
	"acceptance/ld_hl_sp_e_timing", "acceptance/pop_timing", "acceptance/push_timing", "acceptance/rapid_di_ei",
	"acceptance/ret_cc_timing", "acceptance/ret_timing", "acceptance/reti_intr_timing", "acceptance/reti_timing",
	"acceptance/rst_timing", "acceptance/instr/daa", "acceptance/interrupts/ie_push", "acceptance/bits/mem_oam",
	"acceptance/bits/reg_f"));

INSTANTIATE_TEST_CASE_P(OamDmaController, MooneyeCgbTest, testing::Values(
	"acceptance/oam_dma_restart", "acceptance/oam_dma_start", "acceptance/oam_dma_timing", "acceptance/oam_dma/basic",
	"acceptance/oam_dma/reg_read"));

INSTANTIATE_TEST_CASE_P(Ppu, MooneyeCgbTest, testing::Values(
	"acceptance/ppu/intr_2_0_timing", "acceptance/ppu/intr_2_mode0_timing_sprites", "acceptance/ppu/intr_2_mode0_timing",
	"acceptance/ppu/intr_2_mode3_timing", "acceptance/ppu/intr_2_oam_ok_timing", "acceptance/ppu/stat_irq_blocking",
	"acceptance/ppu/stat_lyc_onoff"));

INSTANTIATE_TEST_CASE_P(DividerTimer, MooneyeCgbTest, testing::Values(
	"acceptance/timer/div_write", "acceptance/timer/rapid_toggle", "acceptance/timer/tim00_div_trigger",
	"acceptance/timer/tim00", "acceptance/timer/tim01_div_trigger", "acceptance/timer/tim01",
	"acceptance/timer/tim10_div_trigger", "acceptance/timer/tim10", "acceptance/timer/tim11_div_trigger",
	"acceptance/timer/tim11", "acceptance/timer/tima_reload", "acceptance/timer/tima_write_reloading",
	"acceptance/timer/tma_write_reloading"));
