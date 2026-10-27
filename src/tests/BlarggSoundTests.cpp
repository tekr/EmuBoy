#include "stdafx.h"
#include <gtest/gtest.h>
#include <string>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// Runs blargg's dmg_sound test ROMs (see roms/readme.md). Each ROM writes its status to cartridge RAM at
// $A000: 0x80 while running, then the result code (0 = passed), with the signature DE B0 61 at $A001-$A003
// and its text output as a zero-terminated string from $A004.

class BlarggSoundTest : public testing::TestWithParam<const char*>
{
};

TEST_P(BlarggSoundTest, Passes)
{
	SCOPED_TRACE(GetParam());
	auto cartridge = CartridgeFactory::LoadFromFile(std::string("roms/dmg_sound/") + GetParam() + ".gb", Cartridge::RamBankSize);
	ASSERT_NE(nullptr, cartridge) << "ROM not found; run the tests from src/tests";

	Emulator emulator{ cartridge };
	auto& memory = emulator.GetMemoryMap();

	const auto maxFrames = 60 * 60; // a minute of emulated time

	for (auto frame = 0; frame < maxFrames; frame++)
	{
		emulator.GetFrame();

		// The ROM may leave cartridge RAM disabled; enable it so the results can be read
		memory.WriteByte(0x0000, 0x0a);

		auto hasSignature = memory.ReadByte(0xa001) == 0xde && memory.ReadByte(0xa002) == 0xb0 && memory.ReadByte(0xa003) == 0x61;
		auto status = memory.ReadByte(0xa000);

		if (hasSignature && status != 0x80)
		{
			std::string text;
			for (unsigned short address = 0xa004; address < 0xc000 && memory.ReadByte(address) != 0; address++)
			{
				text += static_cast<char>(memory.ReadByte(address));
			}

			EXPECT_EQ(0, status) << text;
			return;
		}
	}

	FAIL() << "No result after " << maxFrames << " frames";
}

// INSTANTIATE_TEST_CASE_P (not _SUITE_P) and no name generator, for the Google Test 1.7 used on Windows
INSTANTIATE_TEST_CASE_P(DmgSound, BlarggSoundTest, testing::Values(
	"01-registers", "02-len ctr", "03-trigger", "04-sweep", "05-sweep details", "06-overflow on trigger",
	"07-len sweep period sync", "08-len ctr during power", "09-wave read while on", "10-wave trigger while on",
	"11-regs after power", "12-wave write while on"));
