
#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstring>
#include <new>
#include "GraphicsTestFixture.h"
#include "TestGraphics.h"

static_assert(TestGraphics::RegisterBlockSize == MemoryMap::UnusableArea2 - MemoryMap::VramRegisters,
	"Graphics must store every register that MemoryMap routes to it");

TEST_F(GraphicsTestFixture, WindowXRegisterKeepsWrittenValue)
{
	const unsigned char windowX = 0x57;

	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX, windowX);
	DrawFirstLine(0);
	for (auto i = 0; i < 3; i++) DrawNextLine();

	EXPECT_EQ(windowX, Memory.ReadByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX));
}

TEST_F(GraphicsTestFixture, WindowDrawnFromWindowXPosition)
{
	const unsigned short tileBytes = 16;
	const unsigned short tileMapBytes = 32 * 32;
	const unsigned short backgroundTileMap = MemoryMap::RamVideo + TestGraphics::TileMapBase1;
	const unsigned short windowTileMap = MemoryMap::RamVideo + TestGraphics::TileMapBase2;
	const unsigned char identityPalette = 0xe4;
	const unsigned char backgroundTile = 0;
	const unsigned char windowTile = 1;
	const unsigned char allPixelsColour0 = 0;
	const unsigned char allPixelsColour3 = 0xff;
	const auto windowLeftEdge = 80;
	const auto windowXOffset = 7;

	for (unsigned short i = 0; i < tileBytes; i++)
	{
		Memory.WriteByte(MemoryMap::RamVideo + backgroundTile * tileBytes + i, allPixelsColour0);
		Memory.WriteByte(MemoryMap::RamVideo + windowTile * tileBytes + i, allPixelsColour3);
	}

	for (unsigned short i = 0; i < tileMapBytes; i++)
	{
		Memory.WriteByte(backgroundTileMap + i, backgroundTile);
		Memory.WriteByte(windowTileMap + i, windowTile);
	}

	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgScrollY, 0);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgScrollX, 0);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgWinPalette, identityPalette);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowY, 0);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX, windowLeftEdge + windowXOffset);

	// Display, window (map at windowTileMap), tile data at RamVideo, background (map at backgroundTileMap)
	DrawFirstLine(0x40 | 0x20 | 0x10 | 0x01);

	auto background = Ppu.Bitmap[0];
	auto window = Ppu.Bitmap[Graphics::HozPixels - 1];

	ASSERT_NE(background, window);

	for (auto x = 0; x < Graphics::HozPixels; x++)
	{
		EXPECT_EQ(x < windowLeftEdge ? background : window, Ppu.Bitmap[x]) << "at x = " << x;
	}
}

namespace
{
	const unsigned short LcdControlAddress = MemoryMap::VramRegisters + TestGraphics::RegLcdControl;
	const unsigned short LineCountAddress = MemoryMap::VramRegisters + TestGraphics::RegLineCount;
}

TEST_F(GraphicsTestFixture, LineCountIsLineBeingDrawn)
{
	TurnOn();
	EXPECT_EQ(0, Memory.ReadByte(LineCountAddress));

	for (unsigned int line = 1; line < Graphics::FrameLines; line++)
	{
		RunTo(line, 0);
		EXPECT_EQ(line, Memory.ReadByte(LineCountAddress));
	}

	// The last line of the frame is followed by line 0 of the next
	RunTo(Graphics::FrameLines, 0);
	EXPECT_EQ(0, Memory.ReadByte(LineCountAddress));
}

TEST_F(GraphicsTestFixture, LineCountStaysZeroWhileDisplayOff)
{
	Run(3 * Graphics::ScanlineClocks);

	EXPECT_EQ(0, Memory.ReadByte(LineCountAddress));
}

TEST_F(GraphicsTestFixture, BackgroundDisableAlsoBlanksWindow)
{
	// Tile 0 is colour 3, and both tile maps use it
	for (unsigned short i = 0; i < 16; i++) Memory.WriteByte(MemoryMap::RamVideo + i, 0xff);
	for (unsigned short i = 0; i < 2 * 32 * 32; i++) Memory.WriteByte(MemoryMap::RamVideo + TestGraphics::TileMapBase1 + i, 0);

	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgWinPalette, 0xe4);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowY, 0);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX, 7);

	// Display, window and tile data at 0x8000 on, background off
	DrawFirstLine(0x30);

	EXPECT_EQ(Graphics::ShadeColour(0), Ppu.Bitmap[0]);
}

TEST_F(GraphicsTestFixture, WindowLineCounterAdvancesWhileBackgroundDisabled)
{
	// Window rows alternate between colour 3 (even) and colour 0 (odd), so the row drawn shows the counter
	for (unsigned short row = 0; row < 8; row++)
	{
		Memory.WriteByte(MemoryMap::RamVideo + row * 2, row % 2 ? 0 : 0xff);
		Memory.WriteByte(MemoryMap::RamVideo + row * 2 + 1, row % 2 ? 0 : 0xff);
	}
	for (unsigned short i = 0; i < 2 * 32 * 32; i++) Memory.WriteByte(MemoryMap::RamVideo + TestGraphics::TileMapBase1 + i, 0);

	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgWinPalette, 0xe4);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowY, 0);
	Memory.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX, 7);

	// Line 0 with the background (and so the window) blanked, then line 1 with it on again
	DrawFirstLine(0x30);

	Memory.WriteByte(LcdControlAddress, 0xb1);
	DrawNextLine();

	// Window row 1 (colour 0) is drawn on line 1, not row 0 again
	EXPECT_EQ(Graphics::ShadeColour(0), Ppu.Bitmap[Graphics::HozPixels]);
}

TEST_F(GraphicsTestFixture, RegistersHaveDefinedPowerOnValues)
{
	// Construct over non-zero memory, so uninitialised registers would show
	alignas(Graphics) unsigned char storage[sizeof(Graphics)];
	memset(storage, 0xaa, sizeof(storage));
	auto graphics = new (storage) Graphics(Processor, Memory, Sprites);

	// The boot ROM runs from power-on, with the display off. The palettes start as all colour 3, and STAT bit 7
	// always reads as 1
	for (unsigned short address = 0; address < TestGraphics::RegisterBlockSize; address++)
	{
		int expected = address == TestGraphics::RegLcdStatus ? 0x80
			: address == TestGraphics::RegBgWinPalette || address == TestGraphics::RegSprite0Palette || address == TestGraphics::RegSprite1Palette ? 0xff
			: 0;

		EXPECT_EQ(expected, static_cast<int>(graphics->ReadRegister(address))) << "register " << address;
	}

	graphics->~Graphics();
	Memory.SetGraphics(&Ppu);
}

TEST_F(GraphicsTestFixture, WritesToLineCountAreIgnored)
{
	TurnOn();
	RunTo(3, 0);

	Memory.WriteByte(LineCountAddress, 0x55);

	EXPECT_EQ(3, Memory.ReadByte(LineCountAddress));
}
