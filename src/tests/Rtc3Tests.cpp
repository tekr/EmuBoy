#include "stdafx.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "ScreenText.h"

// Runs aaaaaa123456789's rtc3test (see roms/readme.md), which tests the MBC3 real-time clock and shows its results on
// the background tile map at $9C00. Tiles follow the ROM's src/charmap.asm: $00-$3F are characters, plus $40 when
// drawn green or $80 when drawn red (a failure), and $C0-$C4 are symbols.

namespace
{
	const int MaxFramesToMenu = 1000;
	const int MaxFramesPerSuite = 5000;

	bool IsRed(unsigned char tile)
	{
		return tile >= 0x80 && tile < 0xc0;
	}

	char Rtc3Char(unsigned char tile)
	{
		static const std::string symbols = ">*:/-";
		if (tile >= 0xc0) return tile - 0xc0u < symbols.size() ? symbols[tile - 0xc0] : ' ';

		tile &= 0x3f;
		if (tile < 0x0a) return static_cast<char>('0' + tile);
		if (tile < 0x24) return static_cast<char>('A' + tile - 0x0a);
		if (tile < 0x3e) return static_cast<char>('a' + tile - 0x24);
		return tile == 0x3e ? '.' : ' ';
	}

	class Rtc3Test : public testing::Test
	{
	public:
		std::shared_ptr<Cartridge> Rom{ CartridgeFactory::LoadFromFile("roms/rtc3test/rtc3test.gb") };
		std::unique_ptr<Emulator> Emu;

		void SetUp() override
		{
			ASSERT_NE(nullptr, Rom) << "ROM not found; run the tests from src/tests";
			Emu.reset(new Emulator{ Rom });
		}

		std::vector<std::vector<unsigned char>> Tiles() const
		{
			return ScreenText::ReadTiles(Emu->GetMemoryMap(), ScreenText::TileMap1);
		}

		std::string Text() const
		{
			return ScreenText::ToText(Tiles(), Rtc3Char);
		}

		void Press(JoypadKey key)
		{
			Emu->GetJoypad().SetKeysDown(key);
			for (auto frame = 0; frame < 10; frame++) Emu->GetFrame();

			Emu->GetJoypad().SetKeysDown(JoypadKey::NoKey);
			for (auto frame = 0; frame < 10; frame++) Emu->GetFrame();
		}

		// Runs frames until the screen shows the text
		bool WaitFor(const std::string& text, int maxFrames)
		{
			for (auto frame = 0; frame < maxFrames; frame++)
			{
				if (Text().find(text) != std::string::npos) return true;
				Emu->GetFrame();
			}

			return false;
		}

		// Runs the suite selected in the menu, then returns to the menu
		void RunSuite(const std::string& name)
		{
			SCOPED_TRACE(name);
			Press(JoypadKey::A);

			// The results screen offers to return once every test has finished
			ASSERT_TRUE(WaitFor("Return", MaxFramesPerSuite)) << Text();

			// Marks the rows drawn in red, which the text alone doesn't show
			std::string text;
			auto hasRed = false;
			for (auto& row : Tiles())
			{
				auto rowIsRed = std::any_of(row.begin(), row.end(), IsRed);
				hasRed |= rowIsRed;
				text += ScreenText::ToText({ row }, Rtc3Char).insert(row.size(), rowIsRed ? "  <- red" : "");
			}

			EXPECT_NE(std::string::npos, text.find(name)) << text;
			EXPECT_EQ(std::string::npos, text.find("FAIL")) << text;
			EXPECT_FALSE(hasRed) << "Results shown in red:\n" << text;

			Press(JoypadKey::A);
			ASSERT_TRUE(WaitFor("Run tests", MaxFramesToMenu)) << Text();
		}
	};
}

TEST_F(Rtc3Test, AllSuitesPass)
{
	ASSERT_TRUE(WaitFor("Run tests", MaxFramesToMenu)) << Text();

	RunSuite("Basic tests");

	// The cursor stays on the last suite run
	Press(JoypadKey::Down);
	RunSuite("Range tests");

	Press(JoypadKey::Down);
	RunSuite("Sub-second writes");
}
