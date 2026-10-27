#include "stdafx.h"
#include <gtest/gtest.h>
#include "GraphicsTestFixture.h"
#include "../core/CgbColourCorrection.h"
#include "../core/CgbPalettes.h"

// The PPU in CGB mode: colour palettes and their access rules (https://gbdev.io/pandocs/Palettes.html), background map
// attributes and BG-to-OBJ priority (https://gbdev.io/pandocs/Tile_Maps.html), the CGB's object attributes
// (https://gbdev.io/pandocs/OAM.html), object ordering by OPRI (https://gbdev.io/pandocs/CGB_Registers.html), and the
// DMG's bugs it doesn't have

namespace
{
	const unsigned short Bcps = 0xff68, Bcpd = 0xff69, Ocps = 0xff6a, Ocpd = 0xff6b, Opri = 0xff6c, Vbk = 0xff4f;

	// LCDC: LCD on, BG/window master priority (bit 0), objects on, tile data at $8000, window on at $9C00
	const unsigned char MasterPriority = 0x01, ObjectsOn = 0x02, TilesAt8000 = 0x10, WindowOn = 0x20, WindowMap9c00 = 0x40;
	const unsigned char Lcdc = MasterPriority | ObjectsOn | TilesAt8000;

	// Background map attributes
	const unsigned char AttrBank1 = 0x08, AttrXFlip = 0x20, AttrYFlip = 0x40, AttrBgOverObjects = 0x80;

	const unsigned short Red = 0x001f, Green = 0x03e0, Blue = 0x7c00, White = 0x7fff, Grey = 0x4210, Yellow = 0x03ff;

	class CgbGraphicsTest : public GraphicsTestFixture
	{
	public:
		CgbGraphicsTest()
		{
			Ppu.SetCgbMode(true);
			Memory.SetCgbMode(true);
		}

		unsigned char Read(unsigned short address) { return Memory.ReadByte(address); }
		void Write(unsigned short address, unsigned char value) { Memory.WriteByte(address, value); }

		void SetColour(unsigned short indexRegister, int palette, int colour, unsigned short rgb)
		{
			Write(indexRegister, static_cast<unsigned char>(0x80 | (palette * 4 + colour) * 2));
			Write(static_cast<unsigned short>(indexRegister + 1), rgb & 0xff);
			Write(static_cast<unsigned short>(indexRegister + 1), rgb >> 8);
		}

		void SetBgColour(int palette, int colour, unsigned short rgb) { SetColour(Bcps, palette, colour, rgb); }
		void SetObjectColour(int palette, int colour, unsigned short rgb) { SetColour(Ocps, palette, colour, rgb); }

		void WriteVram(int bank, unsigned short address, unsigned char value)
		{
			Write(Vbk, static_cast<unsigned char>(bank));
			Ppu.Vram(address) = value;
			Write(Vbk, 0);
		}

		// Every row of the tile at $8000 + tile * 16 in the bank, given its two bit planes
		void FillTile(int bank, int tile, unsigned char low, unsigned char high)
		{
			for (auto row = 0; row < 8; row++) SetTileRow(bank, tile, row, low, high);
		}

		void SetTileRow(int bank, int tile, int row, unsigned char low, unsigned char high)
		{
			WriteVram(bank, static_cast<unsigned short>(tile * 16 + row * 2), low);
			WriteVram(bank, static_cast<unsigned short>(tile * 16 + row * 2 + 1), high);
		}

		// The tile number and attributes of a background map entry at $9800
		void SetMapEntry(int column, unsigned char tile, unsigned char attributes, unsigned short mapBase = 0x1800)
		{
			WriteVram(0, static_cast<unsigned short>(mapBase + column), tile);
			WriteVram(1, static_cast<unsigned short>(mapBase + column), attributes);
		}

		// An object on line 0 at screen X x
		void SetObject(int index, int x, unsigned char tile, unsigned char flags)
		{
			auto address = static_cast<unsigned short>(index * 4);
			Ppu.WriteOamDirect(address, 16);
			Ppu.WriteOamDirect(address + 1, static_cast<unsigned char>(x + 8));
			Ppu.WriteOamDirect(address + 2, tile);
			Ppu.WriteOamDirect(address + 3, flags);
		}

		unsigned short Pixel(int x, int y = 0) const { return Ppu.CgbFrameRgb555()[y * Graphics::HozPixels + x]; }
	};
}

TEST(CgbPalettesTest, IndexKeepsAutoIncrementAndAddressWithBit6ReadingOne)
{
	CgbPalettes palettes;
	EXPECT_EQ(0x40, palettes.ReadIndex(CgbPalettes::Set::Background));
	palettes.WriteIndex(CgbPalettes::Set::Background, 0xbf);
	EXPECT_EQ(0xff, palettes.ReadIndex(CgbPalettes::Set::Background));
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x55);
	EXPECT_EQ(0x55, palettes.ReadIndex(CgbPalettes::Set::Background));
}

TEST(CgbPalettesTest, DataWritesAdvanceAnAutoIncrementingIndexButReadsDont)
{
	CgbPalettes palettes;
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x80 | 0x10);
	palettes.WriteData(CgbPalettes::Set::Background, 0x12, false);
	palettes.WriteData(CgbPalettes::Set::Background, 0x34, false);
	EXPECT_EQ(0xc0 | 0x12, palettes.ReadIndex(CgbPalettes::Set::Background));

	palettes.ReadData(CgbPalettes::Set::Background, false);
	EXPECT_EQ(0xc0 | 0x12, palettes.ReadIndex(CgbPalettes::Set::Background));

	palettes.WriteIndex(CgbPalettes::Set::Background, 0x10);
	EXPECT_EQ(0x12, palettes.ReadData(CgbPalettes::Set::Background, false));
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x11);
	EXPECT_EQ(0x34, palettes.ReadData(CgbPalettes::Set::Background, false));
}

TEST(CgbPalettesTest, WithoutAutoIncrementTheIndexStays)
{
	CgbPalettes palettes;
	palettes.WriteIndex(CgbPalettes::Set::Objects, 0x07);
	palettes.WriteData(CgbPalettes::Set::Objects, 0x11, false);
	palettes.WriteData(CgbPalettes::Set::Objects, 0x22, false);
	EXPECT_EQ(0x47, palettes.ReadIndex(CgbPalettes::Set::Objects));
	EXPECT_EQ(0x22, palettes.ReadData(CgbPalettes::Set::Objects, false));
}

TEST(CgbPalettesTest, TheAddressWrapsFrom63To0)
{
	CgbPalettes palettes;
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x80 | 63);
	palettes.WriteData(CgbPalettes::Set::Background, 0xab, false);
	EXPECT_EQ(0xc0, palettes.ReadIndex(CgbPalettes::Set::Background));
	palettes.WriteData(CgbPalettes::Set::Background, 0xcd, false);

	palettes.WriteIndex(CgbPalettes::Set::Background, 63);
	EXPECT_EQ(0xab, palettes.ReadData(CgbPalettes::Set::Background, false));
	palettes.WriteIndex(CgbPalettes::Set::Background, 0);
	EXPECT_EQ(0xcd, palettes.ReadData(CgbPalettes::Set::Background, false));
}

TEST(CgbPalettesTest, BlockedAccessReadsFfAndLosesWritesButStillAdvances)
{
	CgbPalettes palettes;
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x80);
	palettes.WriteData(CgbPalettes::Set::Background, 0x99, true);
	EXPECT_EQ(0xc1, palettes.ReadIndex(CgbPalettes::Set::Background));
	EXPECT_EQ(0xff, palettes.ReadData(CgbPalettes::Set::Background, true));

	palettes.WriteIndex(CgbPalettes::Set::Background, 0x00);
	EXPECT_EQ(0x00, palettes.ReadData(CgbPalettes::Set::Background, false));
}

TEST(CgbPalettesTest, BackgroundAndObjectPalettesAreSeparate)
{
	CgbPalettes palettes;
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x80);
	palettes.WriteData(CgbPalettes::Set::Background, 0x11, false);
	palettes.WriteIndex(CgbPalettes::Set::Objects, 0x80);
	palettes.WriteData(CgbPalettes::Set::Objects, 0x22, false);

	EXPECT_EQ(0x11, palettes.Colour(CgbPalettes::Set::Background, 0, 0));
	EXPECT_EQ(0x22, palettes.Colour(CgbPalettes::Set::Objects, 0, 0));
	EXPECT_EQ(0x81, palettes.ReadIndex(CgbPalettes::Set::Background) & 0xbf);
}

TEST(CgbPalettesTest, ColoursAreLittleEndianWithBit15Ignored)
{
	CgbPalettes palettes;
	palettes.WriteIndex(CgbPalettes::Set::Background, 0x80 | (5 * 4 + 2) * 2);
	palettes.WriteData(CgbPalettes::Set::Background, 0x34, false);
	palettes.WriteData(CgbPalettes::Set::Background, 0xf2, false);
	EXPECT_EQ(0x7234, palettes.Colour(CgbPalettes::Set::Background, 5, 2));
}

TEST(CgbColourCorrectionTest, MatchesPokefan531sModel)
{
	// Worked out from the model at commit a21d4d4 (see CgbColourCorrection.cpp) separately, in Python: white is a
	// little dimmer than raw (243 rather than 255), mid-grey lighter (162 rather than 132), and pure red, green and blue
	// lose saturation as the channels mix
	auto rgb = [](int r, int g, int b) { return static_cast<int>(0xcc000000u | static_cast<unsigned int>(b << 16 | g << 8 | r)); };
	EXPECT_EQ(rgb(243, 243, 243), CgbColourCorrection::Pixel(White));
	EXPECT_EQ(rgb(0, 0, 0), CgbColourCorrection::Pixel(0x0000));
	EXPECT_EQ(rgb(162, 162, 162), CgbColourCorrection::Pixel(Grey));
	EXPECT_EQ(rgb(92, 92, 92), CgbColourCorrection::Pixel(0x2108));
	EXPECT_EQ(rgb(231, 89, 101), CgbColourCorrection::Pixel(Red));
	EXPECT_EQ(rgb(122, 197, 137), CgbColourCorrection::Pixel(Green));
	EXPECT_EQ(rgb(0, 132, 188), CgbColourCorrection::Pixel(Blue));
}

TEST(CgbColourCorrectionTest, IgnoresBit15)
{
	EXPECT_EQ(CgbColourCorrection::Pixel(0x1234), CgbColourCorrection::Pixel(0x9234));
}

TEST(CgbColourCorrectionTest, TheLcdOffColourIsTheModelsWhite)
{
	EXPECT_EQ(CgbColourCorrection::Pixel(White), CgbColourCorrection::LcdOffPixel());
}

TEST_F(CgbGraphicsTest, PaletteDataIsBlockedInMode3)
{
	SetBgColour(0, 0, Red);
	TurnOn(Lcdc);
	RunTo(0, TestGraphics::DrawStart);

	Write(Bcps, 0x80);
	EXPECT_EQ(0xff, Read(Bcpd));
	Write(Bcpd, 0x00);
	EXPECT_EQ(0xc1, Read(Bcps));
	EXPECT_EQ(0xff, Read(Ocpd));

	// In H-Blank it can be accessed again, and the write was lost
	RunTo(1, 0);
	Write(Bcps, 0x00);
	EXPECT_EQ(Red & 0xff, Read(Bcpd));
}

TEST_F(CgbGraphicsTest, PaletteDataCanBeAccessedOutsideMode3)
{
	TurnOn(Lcdc);
	RunTo(0, 40);
	Write(Bcps, 0x80);
	Write(Bcpd, 0x12);
	Write(Bcps, 0x00);
	EXPECT_EQ(0x12, Read(Bcpd));
}

TEST_F(CgbGraphicsTest, BackgroundTilesUseTheirAttributesPalette)
{
	FillTile(0, 1, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetBgColour(3, 1, Green);
	SetMapEntry(0, 1, 3);
	SetMapEntry(1, 1, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Green, Pixel(0));
	EXPECT_EQ(Green, Pixel(7));
	EXPECT_EQ(Red, Pixel(8));
}

TEST_F(CgbGraphicsTest, BackgroundTilesComeFromTheirAttributesVramBank)
{
	FillTile(0, 1, 0xff, 0x00);
	FillTile(1, 1, 0x00, 0xff);
	SetBgColour(0, 1, Red);
	SetBgColour(0, 2, Blue);
	SetMapEntry(0, 1, AttrBank1);
	SetMapEntry(1, 1, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Blue, Pixel(0));
	EXPECT_EQ(Red, Pixel(8));
}

TEST_F(CgbGraphicsTest, BackgroundTilesFlipHorizontally)
{
	FillTile(0, 1, 0x80, 0x00);
	SetBgColour(0, 0, White);
	SetBgColour(0, 1, Red);
	SetMapEntry(0, 1, AttrXFlip);
	SetMapEntry(1, 1, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(White, Pixel(0));
	EXPECT_EQ(Red, Pixel(7));
	EXPECT_EQ(Red, Pixel(8));
	EXPECT_EQ(White, Pixel(15));
}

TEST_F(CgbGraphicsTest, BackgroundTilesFlipVertically)
{
	SetTileRow(0, 1, 0, 0x00, 0x00);
	SetTileRow(0, 1, 7, 0xff, 0x00);
	SetBgColour(0, 0, White);
	SetBgColour(0, 1, Red);
	SetMapEntry(0, 1, AttrYFlip);
	SetMapEntry(1, 1, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Red, Pixel(0));
	EXPECT_EQ(White, Pixel(8));
}

TEST_F(CgbGraphicsTest, TheWindowUsesItsMapsAttributes)
{
	FillTile(0, 1, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetBgColour(6, 1, Blue);
	SetMapEntry(0, 1, 0);
	SetMapEntry(0, 1, 6, 0x1c00);
	WriteRegister(TestGraphics::RegWindowY, 0);
	WriteRegister(TestGraphics::RegWindowX, 7);

	DrawFirstLine(Lcdc | WindowOn | WindowMap9c00);

	EXPECT_EQ(Blue, Pixel(0));
}

TEST_F(CgbGraphicsTest, ObjectsUseTheirCgbPaletteAndIgnoreTheDmgPaletteBit)
{
	FillTile(0, 2, 0xff, 0x00);
	SetObjectColour(5, 1, Green);
	SetObjectColour(0, 1, Red);
	SetObjectColour(1, 1, Blue);
	SetObject(0, 0, 2, 0x05 | SpriteFlags::PaletteSelector);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Green, Pixel(0));
}

TEST_F(CgbGraphicsTest, ObjectsComeFromTheirVramBank)
{
	FillTile(0, 2, 0xff, 0x00);
	FillTile(1, 2, 0x00, 0xff);
	SetObjectColour(0, 1, Red);
	SetObjectColour(0, 2, Blue);
	SetObject(0, 0, 2, SpriteFlags::CgbVramBank);
	SetObject(1, 8, 2, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Blue, Pixel(0));
	EXPECT_EQ(Red, Pixel(8));
}

TEST_F(CgbGraphicsTest, ObjectColour0IsTransparent)
{
	FillTile(0, 1, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetMapEntry(0, 1, 0);
	SetObjectColour(0, 0, Blue);
	SetObject(0, 0, 0, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Red, Pixel(0));
}

TEST_F(CgbGraphicsTest, TheBgAttributesPriorityPutsBgColours1To3OverObjects)
{
	FillTile(0, 1, 0xff, 0x00);
	FillTile(0, 2, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetObjectColour(0, 1, Blue);
	SetMapEntry(0, 1, AttrBgOverObjects);
	SetMapEntry(1, 0, AttrBgOverObjects);
	SetObject(0, 0, 2, 0);
	SetObject(1, 8, 2, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Red, Pixel(0));
	// BG colour 0 is always behind objects
	EXPECT_EQ(Blue, Pixel(8));
}

TEST_F(CgbGraphicsTest, TheObjectsPriorityPutsItBehindBgColours1To3)
{
	FillTile(0, 1, 0xff, 0x00);
	FillTile(0, 2, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetObjectColour(0, 1, Blue);
	SetMapEntry(0, 1, 0);
	SetMapEntry(1, 0, 0);
	SetMapEntry(2, 1, 0);
	SetObject(0, 0, 2, SpriteFlags::ZPriority);
	SetObject(1, 8, 2, SpriteFlags::ZPriority);
	SetObject(2, 16, 2, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Red, Pixel(0));
	EXPECT_EQ(Blue, Pixel(8));
	EXPECT_EQ(Blue, Pixel(16));
}

TEST_F(CgbGraphicsTest, ClearingLcdcBit0PutsObjectsOnTopButStillShowsTheBackground)
{
	FillTile(0, 1, 0xff, 0x00);
	FillTile(0, 2, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetObjectColour(0, 1, Blue);
	SetMapEntry(0, 1, AttrBgOverObjects);
	SetMapEntry(1, 1, 0);
	SetObject(0, 0, 2, SpriteFlags::ZPriority);

	DrawFirstLine(Lcdc & ~MasterPriority);

	EXPECT_EQ(Blue, Pixel(0));
	EXPECT_EQ(Red, Pixel(8));
}

TEST_F(CgbGraphicsTest, ObjectsArePrioritisedByOamPositionWhenOpriIsClear)
{
	// Object 0 is further right than object 1, but comes first in OAM
	FillTile(0, 2, 0xff, 0x00);
	SetObjectColour(0, 1, Red);
	SetObjectColour(1, 1, Blue);
	SetObject(0, 4, 2, 0);
	SetObject(1, 0, 2, 1);
	Write(Opri, 0x00);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(Blue, Pixel(3));
	EXPECT_EQ(Red, Pixel(4));
	EXPECT_EQ(Red, Pixel(11));
}

TEST_F(CgbGraphicsTest, SettingOpriAfterTheBootRomKeepsPriorityByOamPosition)
{
	// Only the boot ROM's write to OPRI takes effect (Pan Docs; SameBoy and Gambatte), so X ordering isn't selected
	FillTile(0, 2, 0xff, 0x00);
	SetObjectColour(0, 1, Red);
	SetObjectColour(1, 1, Blue);
	SetObject(0, 4, 2, 0);
	SetObject(1, 0, 2, 1);
	Write(Opri, 0x01);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(0xff, Read(Opri)) << "The write is still kept for reading back";
	EXPECT_EQ(Blue, Pixel(3));
	EXPECT_EQ(Red, Pixel(4));
}

TEST_F(CgbGraphicsTest, OpriKeepsOnlyBit0)
{
	EXPECT_EQ(0xfe, Read(Opri));
	Write(Opri, 0xff);
	EXPECT_EQ(0xff, Read(Opri));
	Write(Opri, 0x02);
	EXPECT_EQ(0xfe, Read(Opri));
}

TEST_F(CgbGraphicsTest, TheScreenShowsTheFrameColourCorrected)
{
	FillTile(0, 1, 0xff, 0x00);
	SetBgColour(0, 1, Yellow);
	SetMapEntry(0, 1, 0);

	DrawFirstLine(Lcdc);

	EXPECT_EQ(CgbColourCorrection::Pixel(Yellow), Ppu.Bitmap[0]);
}

TEST_F(CgbGraphicsTest, TurningTheLcdOffBlanksTheScreenToItsWhite)
{
	DrawFirstLine(Lcdc);
	WriteRegister(TestGraphics::RegLcdControl, 0);
	Run(Graphics::FrameClocks);

	EXPECT_EQ(Graphics::LcdOffRgb555, Pixel(0));
	EXPECT_EQ(CgbColourCorrection::LcdOffPixel(), Ppu.Bitmap[0]);
}

TEST_F(CgbGraphicsTest, OamIsntCorruptedByTheDmgsBug)
{
	for (unsigned short address = 0; address < Graphics::OamSize; address++) Ppu.WriteOamDirect(address, static_cast<unsigned char>(address));

	// Part-way through line 0's OAM scan of the second frame (the first line after turning on has no scan), where
	// the DMG would corrupt a row
	TurnOn(Lcdc);
	RunTo(1, TestGraphics::OamScanStart + 40);
	Ppu.CorruptOam(OamCorruption::Write);
	Ppu.CorruptOam(OamCorruption::Read);

	for (unsigned short address = 0; address < Graphics::OamSize; address++) EXPECT_EQ(address, Ppu.OamByte(address));
}

TEST_F(CgbGraphicsTest, WritingStatDoesntRequestAnInterruptAsTheDmgsBugDoes)
{
	// In V-Blank the DMG's bug sees the V-Blank interrupt select set by the write's $FF
	TurnOn(Lcdc);
	RunTo(145, 100);
	Processor.WaitingInterrupts() = 0;

	WriteRegister(TestGraphics::RegLcdStatus, 0x00);

	EXPECT_EQ(0, Processor.WaitingInterrupts() & InterruptFlags::LcdStatInt);
}

TEST_F(GraphicsTestFixture, TheDmgsStatWriteBugStillRequestsAnInterrupt)
{
	TurnOn(0x80);
	RunTo(145, 100);
	Processor.WaitingInterrupts() = 0;

	WriteRegister(TestGraphics::RegLcdStatus, 0x00);

	EXPECT_EQ(InterruptFlags::LcdStatInt, Processor.WaitingInterrupts() & InterruptFlags::LcdStatInt);
}


TEST_F(CgbGraphicsTest, AStoppedCgbKeepsShowingItsLastFrame)
{
	// The DMG shows its lightest shade while STOP has stopped the PPU; a CGB keeps its picture (SameBoy's
	// GB_display_vblank stops the PPU's output only on the DMG)
	FillTile(0, 1, 0xff, 0x00);
	SetBgColour(0, 1, Red);
	SetMapEntry(0, 1, 0);
	DrawFirstLine(Lcdc);

	Ppu.RunStoppedCycles(Graphics::FrameClocks);

	EXPECT_TRUE(Ppu.TakeCompletedFrame());
	EXPECT_EQ(Red, Pixel(0));
	EXPECT_EQ(CgbColourCorrection::Pixel(Red), Ppu.Bitmap[0]);
}

TEST_F(CgbGraphicsTest, AStoppedCgbWithTheLcdOffShowsTheLcdOffColour)
{
	DrawFirstLine(Lcdc);
	WriteRegister(TestGraphics::RegLcdControl, 0);

	Ppu.RunStoppedCycles(Graphics::FrameClocks);

	EXPECT_EQ(Graphics::LcdOffRgb555, Pixel(0));
	EXPECT_EQ(CgbColourCorrection::LcdOffPixel(), Ppu.Bitmap[0]);
}
