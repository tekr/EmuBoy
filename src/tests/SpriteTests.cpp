#include "stdafx.h"
#include <gtest/gtest.h>
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include "TestGraphics.h"
#include "../core/SpriteManager.h"

const unsigned short LcdControlReg = 0x0;
const unsigned short LcdStatusReg = 0x1;
const unsigned short LineCountReg = 0x4;
const unsigned short BgScrollYReg = 0x2;
const unsigned short BgScrollXReg = 0x3;
const unsigned short BgWinPaletteReg = 0x7;
const unsigned short Sprite0PaletteReg = 0x8;
const unsigned short Sprite1PaletteReg = 0x9;

// Display, background and 8x8 sprites on, tile data at 0x8000 (shared with sprites), BG tile map at 0x9800
const unsigned char LcdControl = 0x93;
const unsigned char DisplayOn = 0x80;

// Tile 0 is used by the background. Sprite tiles: solid colour 1, and left half transparent with right half colour 1
const unsigned char SolidTile = 1;
const unsigned char RightHalfTile = 2;

// Sprite X that puts a sprite at screen X 0, and sprite Y that puts it on line 0
const unsigned char ScreenLeft = SpriteManager::SpriteXOffset;
const unsigned char FirstLine = SpriteManager::SpriteYOffset;

class SpriteTests : public testing::Test
{
public:
	InputJoypad Joypad;
	TestMemoryMap Memory{ Joypad };
	TestCpu Processor{ Memory };
	SpriteManager Sprites{};
	TestGraphics Ppu{ Processor, Memory, Sprites };

	SpriteTests()
	{
		// VRAM and OAM aren't initialised. Each OAM byte is written twice so that every sprite is
		// seen to move, and so is known to the sprite manager
		for (auto i = 0; i < Graphics::VramSize; i++) Ppu.Vram(i) = 0;
		for (auto i = 0; i < Graphics::OamSize; i++)
		{
			Ppu.WriteOam(i, 0xff);
			Ppu.WriteOam(i, 0);
		}

		SetTile(SolidTile, 0xff);
		SetTile(RightHalfTile, 0x0f);

		Ppu.WriteRegister(LcdControlReg, LcdControl & ~DisplayOn);
		Ppu.WriteRegister(BgScrollYReg, 0);
		Ppu.WriteRegister(BgScrollXReg, 0);

		// Colour 1 is a different shade in each palette
		Ppu.WriteRegister(BgWinPaletteReg, 0x04);
		Ppu.WriteRegister(Sprite0PaletteReg, 0x08);
		Ppu.WriteRegister(Sprite1PaletteReg, 0x0c);
	}

	// Sets every row of a tile to colour 1 where a bit of the pattern is set, else colour 0
	void SetTile(int tile, unsigned char pattern)
	{
		for (auto row = 0; row < 8; row++)
		{
			Ppu.Vram(tile * 16 + row * 2) = pattern;
			Ppu.Vram(tile * 16 + row * 2 + 1) = 0;
		}
	}

	void SetSprite(int index, unsigned char x, unsigned char tile, unsigned char flags = 0)
	{
		Ppu.WriteOam(index * 4, FirstLine);
		Ppu.WriteOam(index * 4 + 1, x);
		Ppu.WriteOam(index * 4 + 2, tile);
		Ppu.WriteOam(index * 4 + 3, flags);
	}

	void Run(int clocks)
	{
		for (; clocks > 0; clocks -= 4) Ppu.RunCycles(4);
	}

	// Turns the LCD on, with the other LCDC bits as they are, and runs until line 0 is drawn as its mode 3 starts
	void RenderFirstLine()
	{
		auto lcdControl = Ppu.ReadRegister(LcdControlReg);
		Ppu.WriteRegister(LcdControlReg, lcdControl & ~DisplayOn);
		Ppu.WriteRegister(LcdControlReg, lcdControl | DisplayOn);
		Run(TestGraphics::DrawStart - TestGraphics::FirstLineStart);
	}

	// Runs to the current line's H-Blank, when the CPU can write OAM again
	void RunToHBlank()
	{
		for (auto clocks = 0; (Ppu.ReadRegister(LcdStatusReg) & 3) != LcdcStatus::HBlankMode; clocks += 4)
		{
			ASSERT_LT(clocks, static_cast<int>(Graphics::ScanlineClocks)) << "no H-Blank";
			Run(4);
		}
	}

	// Runs until the next line is drawn
	void RenderNextLine()
	{
		auto line = Ppu.ReadRegister(LineCountReg);
		for (auto clocks = 0; Ppu.ReadRegister(LineCountReg) == line; clocks += 4)
		{
			ASSERT_LT(clocks, static_cast<int>(Graphics::ScanlineClocks)) << "LY didn't change";
			Run(4);
		}

		Run(TestGraphics::DrawStart);
	}

	int Pixel(int x, int y = 0) const { return Ppu.Bitmap[y * Graphics::HozPixels + x]; }
	int Colour(unsigned char colour, TestGraphics::Palette palette) { return Ppu.MapColour(colour, palette); }
};

TEST_F(SpriteTests, UsesPaletteOfSpriteDrawn)
{
	// Sprite 1 shows through sprite 0's transparent left half
	SetSprite(0, ScreenLeft, RightHalfTile, SpriteFlags::PaletteSelector);
	SetSprite(1, ScreenLeft, SolidTile);

	RenderFirstLine();

	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(0));
	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite1), Pixel(4));
}

TEST_F(SpriteTests, DrawsOnlyFirstTenSpritesOnLineInOamOrder)
{
	// Sprite 10, the 11th in OAM, is leftmost
	for (auto i = 0; i <= 10; i++) SetSprite(i, ScreenLeft + 8 * (10 - i), SolidTile);

	RenderFirstLine();

	EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(0));
	for (auto i = 0; i < 10; i++) EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(8 * (10 - i))) << "sprite " << i;
}

TEST_F(SpriteTests, OffScreenSpriteCountsTowardsTenSpriteLimit)
{
	SetSprite(0, 0, SolidTile);
	for (auto i = 1; i <= 10; i++) SetSprite(i, ScreenLeft + 8 * (i - 1), SolidTile);

	RenderFirstLine();

	for (auto i = 1; i < 10; i++) EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(8 * (i - 1))) << "sprite " << i;
	EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(72));
}

TEST_F(SpriteTests, TransparentPixelOfBehindBgSpriteShowsLowerPrioritySprite)
{
	SetTile(0, 0xff);
	SetSprite(0, ScreenLeft, RightHalfTile, SpriteFlags::ZPriority);
	SetSprite(1, ScreenLeft, SolidTile);

	RenderFirstLine();

	// Sprite 0 is transparent on the left, so sprite 1 is drawn over the background
	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(0));

	// On the right, sprite 0's pixel wins over sprite 1 but is behind the non-zero background
	EXPECT_EQ(Colour(1, TestGraphics::Palette::BgAndWindow), Pixel(4));
}

TEST_F(SpriteTests, SpriteMovedAfterLineRenderedIsDrawnAtNewPositionOnNextLine)
{
	SetSprite(0, ScreenLeft, SolidTile);
	RenderFirstLine();

	// Written after line 0 was drawn, in its H-Blank, before line 1 is drawn
	RunToHBlank();
	Ppu.WriteOam(1, ScreenLeft + 16);
	RenderNextLine();

	EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(0, 1));
	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(16, 1));
}

TEST_F(SpriteTests, SpriteMovedInHBlankStaysAtOldPositionOnCurrentLine)
{
	SetSprite(0, ScreenLeft, SolidTile);
	RenderFirstLine();

	// Written in line 0's H-Blank: line 0 has already been drawn
	RunToHBlank();
	Ppu.WriteOam(1, ScreenLeft + 16);
	RenderNextLine();

	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(0, 0));
	EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(16, 0));
	EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(0, 1));
	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(16, 1));
}

TEST_F(SpriteTests, SpriteMovedOntoNextLineIsDrawnThere)
{
	// The sprite starts below line 1, so it isn't on line 1 when line 0 is drawn
	SetSprite(0, ScreenLeft, SolidTile);
	Ppu.WriteOam(0, FirstLine + 8);
	RenderFirstLine();

	RunToHBlank();
	Ppu.WriteOam(0, FirstLine);
	RenderNextLine();

	EXPECT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(0, 1));
}

TEST_F(SpriteTests, LowerHalfOfTallSpriteNotDrawnAfterSwitchToShortSprites)
{
	// 8x16 sprite using tiles 0 (blank) and 1 (solid), with only its lower half on lines 0-7
	Ppu.WriteRegister(LcdControlReg, LcdControl | 0x04);
	SetSprite(0, ScreenLeft, SolidTile);
	Ppu.WriteOam(0, FirstLine - 8);
	RenderFirstLine();
	ASSERT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(0));

	// As an 8x8 sprite it covers only the 8 lines above the screen
	Ppu.WriteRegister(LcdControlReg, LcdControl);
	RenderNextLine();

	for (auto x = 0; x < SpriteManager::SpriteWidth; x++) EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(x, 1)) << "at x = " << x;
}

TEST_F(SpriteTests, YFlippedTallSpriteNotDrawnAfterSwitchToShortSprites)
{
	// 8x16 sprite using tiles 2 (solid) and 3 (blank), Y-flipped so tile 2 is its lower half, on lines 0-7
	SetTile(2, 0xff);
	SetSprite(0, ScreenLeft, 2, SpriteFlags::YFlip);
	Ppu.WriteRegister(LcdControlReg, LcdControl | 0x04);
	Ppu.WriteOam(0, FirstLine - 8);
	RenderFirstLine();
	ASSERT_EQ(Colour(1, TestGraphics::Palette::Sprite0), Pixel(0));

	// As an 8x8 sprite it covers only the 8 lines above the screen
	Ppu.WriteRegister(LcdControlReg, LcdControl);
	RenderNextLine();

	EXPECT_EQ(Colour(0, TestGraphics::Palette::BgAndWindow), Pixel(0, 1));
}
