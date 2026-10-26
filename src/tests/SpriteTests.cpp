#include "stdafx.h"
#include <gtest/gtest.h>
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include "TestGraphics.h"
#include "../core/SpriteManager.h"

const unsigned short LcdControlReg = 0x0;
const unsigned short BgScrollYReg = 0x2;
const unsigned short BgScrollXReg = 0x3;
const unsigned short BgWinPaletteReg = 0x7;
const unsigned short Sprite0PaletteReg = 0x8;
const unsigned short Sprite1PaletteReg = 0x9;

// Display, background and 8x8 sprites on, tile data at 0x8000 (shared with sprites), BG tile map at 0x9800
const unsigned char LcdControl = 0x93;

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
	TestMemoryMap MemoryMap{ Joypad };
	TestCpu Cpu{ MemoryMap };
	SpriteManager SpriteManager{};
	TestGraphics Graphics{ Cpu, MemoryMap, SpriteManager };

	SpriteTests()
	{
		// VRAM and OAM aren't initialised. Each OAM byte is written twice so that every sprite is
		// seen to move, and so is known to the sprite manager
		for (auto i = 0; i < Graphics::VramSize; i++) Graphics.Vram(i) = 0;
		for (auto i = 0; i < Graphics::OamSize; i++)
		{
			Graphics.WriteOam(i, 0xff);
			Graphics.WriteOam(i, 0);
		}

		SetTile(SolidTile, 0xff);
		SetTile(RightHalfTile, 0x0f);

		Graphics.WriteRegister(LcdControlReg, LcdControl);
		Graphics.WriteRegister(BgScrollYReg, 0);
		Graphics.WriteRegister(BgScrollXReg, 0);

		// Colour 1 is a different shade in each palette
		Graphics.WriteRegister(BgWinPaletteReg, 0x04);
		Graphics.WriteRegister(Sprite0PaletteReg, 0x08);
		Graphics.WriteRegister(Sprite1PaletteReg, 0x0c);
	}

	// Sets every row of a tile to colour 1 where a bit of the pattern is set, else colour 0
	void SetTile(int tile, unsigned char pattern)
	{
		for (auto row = 0; row < 8; row++)
		{
			Graphics.Vram(tile * 16 + row * 2) = pattern;
			Graphics.Vram(tile * 16 + row * 2 + 1) = 0;
		}
	}

	void SetSprite(int index, unsigned char x, unsigned char tile, unsigned char flags = 0)
	{
		Graphics.WriteOam(index * 4, FirstLine);
		Graphics.WriteOam(index * 4 + 1, x);
		Graphics.WriteOam(index * 4 + 2, tile);
		Graphics.WriteOam(index * 4 + 3, flags);
	}

	void RenderFirstLine()
	{
		Graphics.ResetFrame();
		Graphics.RenderLine();
	}

	int Pixel(int x) const { return Graphics.Bitmap[x]; }
	int Colour(unsigned char colour, TestGraphics::Palette palette) { return Graphics.MapColour(colour, palette); }
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
