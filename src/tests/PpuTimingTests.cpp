#include "stdafx.h"
#include <gtest/gtest.h>
#include "GraphicsTestFixture.h"

// The PPU's timing, per https://gbdev.io/pandocs/Rendering.html and https://gbdev.io/pandocs/STAT.html, with the
// details that mooneye-test-suite's PPU tests (https://github.com/Gekkio/mooneye-test-suite) measure on a DMG.
// Positions are in clocks since LY changed; line 0 starts at position 4 when the LCD is turned on.

namespace
{
	const unsigned char Mode0IntSelect = 0x08;
	const unsigned char Mode1IntSelect = 0x10;
	const unsigned char Mode2IntSelect = 0x20;
	const unsigned char LycIntSelect = 0x40;
	const unsigned char LycFlag = 0x04;

	const unsigned short OamAddress = 0xfe00;
	const unsigned short VramAddress = 0x8000;

	// Where mode 3 ends on a line without penalties (mooneye-test-suite's lcdon_timing-GS)
	const int HBlankStart = 256;
}

class PpuTimingTests : public GraphicsTestFixture
{
public:
	PpuTimingTests()
	{
		Cpu.WaitingInterrupts() = 0;
	}

	int Mode() { return ReadRegister(TestGraphics::RegLcdStatus) & 3; }
	int Ly() { return ReadRegister(TestGraphics::RegLineCount); }
	bool LycFlagSet() { return (ReadRegister(TestGraphics::RegLcdStatus) & LycFlag) != 0; }

	bool StatRequested() { return (Cpu.WaitingInterrupts() & InterruptFlags::LcdStatInt) != 0; }
	bool VBlankRequested() { return (Cpu.WaitingInterrupts() & InterruptFlags::VBlankInt) != 0; }
	void ClearInterrupts() { Cpu.WaitingInterrupts() = 0; }

	void SetStat(unsigned char value) { WriteRegister(TestGraphics::RegLcdStatus, value); }
	void SetLyc(unsigned char value) { WriteRegister(TestGraphics::RegLineCompare, value); }
};

TEST_F(PpuTimingTests, ModesThroughALine)
{
	TurnOn();

	RunTo(1, 0);
	EXPECT_EQ(1, Ly());
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	RunTo(1, 4);
	EXPECT_EQ(LcdcStatus::OamReadMode, Mode());

	RunTo(1, 80);
	EXPECT_EQ(LcdcStatus::OamReadMode, Mode());

	RunTo(1, 84);
	EXPECT_EQ(LcdcStatus::OamAndVramReadMode, Mode());

	RunTo(1, HBlankStart - 4);
	EXPECT_EQ(LcdcStatus::OamAndVramReadMode, Mode());

	RunTo(1, HBlankStart);
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	RunTo(1, Graphics::ScanlineClocks - 4);
	EXPECT_EQ(1, Ly());
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	RunTo(2, 0);
	EXPECT_EQ(2, Ly());
}

TEST_F(PpuTimingTests, FirstLineAfterTurningOnHasNoOamScanAndIsFourClocksShort)
{
	// Mooneye-test-suite's lcdon_timing-GS
	TurnOn();
	EXPECT_EQ(0, Ly());
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	Run(TestGraphics::DrawStart - TestGraphics::FirstLineStart - 4);
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	Run(4);
	EXPECT_EQ(LcdcStatus::OamAndVramReadMode, Mode());

	Run(Graphics::MinDrawClocks);
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	Run(Graphics::ScanlineClocks - HBlankStart - 4);
	EXPECT_EQ(0, Ly());

	Run(4);
	EXPECT_EQ(1, Ly());
}

TEST_F(PpuTimingTests, VBlankStartsAMachineCycleIntoLine144)
{
	TurnOn();

	RunTo(Graphics::VertPixels, 0);
	EXPECT_EQ(static_cast<int>(Graphics::VertPixels), Ly());
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());
	EXPECT_FALSE(VBlankRequested());

	RunTo(Graphics::VertPixels, 4);
	EXPECT_EQ(LcdcStatus::VBlankMode, Mode());
	EXPECT_TRUE(VBlankRequested());
}

TEST_F(PpuTimingTests, LyReadsZeroForAllButTheFirstMachineCycleOfLine153)
{
	TurnOn();

	RunTo(153, 0);
	EXPECT_EQ(153, Ly());

	RunTo(153, 4);
	EXPECT_EQ(0, Ly());

	RunTo(154, 0);
	EXPECT_EQ(0, Ly());
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());

	RunTo(154, 4);
	EXPECT_EQ(LcdcStatus::OamReadMode, Mode());
}

TEST_F(PpuTimingTests, Line153MatchesLyc153ThenLyc0)
{
	TurnOn();

	SetLyc(153);
	RunTo(153, 0);
	EXPECT_FALSE(LycFlagSet());
	RunTo(153, 4);
	EXPECT_TRUE(LycFlagSet());
	RunTo(153, 8);
	EXPECT_FALSE(LycFlagSet());

	SetLyc(0);
	EXPECT_FALSE(LycFlagSet());
	RunTo(153, 12);
	EXPECT_TRUE(LycFlagSet());

	// Line 0's comparison isn't interrupted as LY changes
	RunTo(154, 0);
	EXPECT_TRUE(LycFlagSet());
}

TEST_F(PpuTimingTests, LycFlagReadsFalseForFirstMachineCycleOfALine)
{
	TurnOn();
	SetLyc(5);

	RunTo(5, 0);
	EXPECT_EQ(5, Ly());
	EXPECT_FALSE(LycFlagSet());

	RunTo(5, 4);
	EXPECT_TRUE(LycFlagSet());

	RunTo(6, 0);
	EXPECT_FALSE(LycFlagSet());
}

TEST_F(PpuTimingTests, LycIsComparedAsSoonAsItIsWritten)
{
	TurnOn();
	SetStat(LycIntSelect);
	RunTo(3, 8);

	SetLyc(3);
	EXPECT_TRUE(LycFlagSet());
	EXPECT_TRUE(StatRequested());

	SetLyc(4);
	EXPECT_FALSE(LycFlagSet());
}

TEST_F(PpuTimingTests, LycFlagAndModeAreReadOnly)
{
	TurnOn();
	SetLyc(1);
	RunTo(0, 8);

	SetStat(0xff);
	EXPECT_EQ(0xf8 | LcdcStatus::HBlankMode, ReadRegister(TestGraphics::RegLcdStatus));
}

TEST_F(PpuTimingTests, ModeInterruptsAreRequestedAsTheModesStart)
{
	TurnOn();

	SetStat(Mode2IntSelect);
	RunTo(1, 0);
	ClearInterrupts();
	RunTo(1, 4);
	EXPECT_TRUE(StatRequested());

	SetStat(Mode0IntSelect);
	RunTo(1, HBlankStart - 4);
	ClearInterrupts();
	RunTo(1, HBlankStart);
	EXPECT_TRUE(StatRequested());

	SetStat(Mode1IntSelect);
	RunTo(Graphics::VertPixels, 0);
	ClearInterrupts();
	RunTo(Graphics::VertPixels, 4);
	EXPECT_TRUE(StatRequested());
}

TEST_F(PpuTimingTests, Mode2SelectAlsoRequestsInterruptAsVBlankStarts)
{
	// Mooneye-test-suite's vblank_stat_intr-GS: at the same time as the V-Blank interrupt
	TurnOn();
	SetStat(Mode2IntSelect);
	RunTo(Graphics::VertPixels, 0);
	ClearInterrupts();

	RunTo(Graphics::VertPixels, 4);
	EXPECT_TRUE(StatRequested());
	EXPECT_TRUE(VBlankRequested());
}

TEST_F(PpuTimingTests, StatInterruptLineHeldHighBlocksFurtherInterrupts)
{
	// Per https://gbdev.io/pandocs/Interrupt_Sources.html#int-48--stat-interrupt, the interrupt is requested only
	// when the OR of the selected conditions rises. With modes 0 and 2 selected, mode 0 runs straight into mode 2
	TurnOn();
	SetStat(Mode0IntSelect | Mode2IntSelect);
	RunTo(1, HBlankStart);
	ClearInterrupts();

	RunTo(2, 80);
	EXPECT_FALSE(StatRequested());

	// Mode 3 drops the line, so the next mode 0 raises it again
	RunTo(2, HBlankStart);
	EXPECT_TRUE(StatRequested());
}

TEST_F(PpuTimingTests, LycMatchHoldingLineHighBlocksModeInterrupts)
{
	// Mooneye-test-suite's stat_irq_blocking
	TurnOn();
	SetLyc(2);
	SetStat(LycIntSelect | Mode0IntSelect);
	RunTo(2, 8);
	ClearInterrupts();

	RunTo(2, HBlankStart);
	EXPECT_FALSE(StatRequested());
}

TEST_F(PpuTimingTests, WritingStatOutsideMode3RequestsSpuriousInterrupt)
{
	// Per https://gbdev.io/pandocs/STAT.html#spurious-stat-interrupts, the DMG behaves as if $FF were written for a
	// machine cycle, so even writing 0 requests an interrupt in modes 0-2
	TurnOn();
	RunTo(1, 8);
	ClearInterrupts();
	SetStat(0);
	EXPECT_TRUE(StatRequested());

	RunTo(1, 100);
	ClearInterrupts();
	SetStat(0);
	EXPECT_FALSE(StatRequested());

	RunTo(1, HBlankStart + 4);
	SetStat(0);
	EXPECT_TRUE(StatRequested());
}

TEST_F(PpuTimingTests, NoSpuriousInterruptWhileLcdIsOff)
{
	SetStat(0);
	EXPECT_FALSE(StatRequested());
}

TEST_F(PpuTimingTests, TurningLcdOffResetsLyAndModeButKeepsLycFlag)
{
	// Mooneye-test-suite's stat_lyc_onoff: the flag keeps its value, and changing LYC has no effect, while the LCD
	// is off
	TurnOn();
	SetLyc(10);
	RunTo(10, 100);
	ASSERT_EQ(LcdcStatus::OamAndVramReadMode, Mode());

	WriteRegister(TestGraphics::RegLcdControl, 0);
	EXPECT_EQ(0, Ly());
	EXPECT_EQ(LcdcStatus::HBlankMode, Mode());
	EXPECT_TRUE(LycFlagSet());

	SetLyc(0);
	EXPECT_TRUE(LycFlagSet());
	Run(Graphics::ScanlineClocks);
	EXPECT_EQ(0, Ly());
}

TEST_F(PpuTimingTests, TurningLcdOnComparesLyZeroStraightAway)
{
	SetLyc(0);
	SetStat(LycIntSelect);

	TurnOn();
	EXPECT_TRUE(LycFlagSet());
	EXPECT_TRUE(StatRequested());
}

TEST_F(PpuTimingTests, TurningLcdOnWithLycMatchAlreadySetRequestsNoInterrupt)
{
	// Mooneye-test-suite's stat_lyc_onoff: the interrupt line stays high from before the LCD was turned off
	TurnOn();
	SetLyc(10);
	SetStat(LycIntSelect);
	RunTo(10, 100);
	WriteRegister(TestGraphics::RegLcdControl, 0);
	SetLyc(0);
	ClearInterrupts();

	TurnOn();
	EXPECT_TRUE(LycFlagSet());
	EXPECT_FALSE(StatRequested());
}

TEST_F(PpuTimingTests, CpuCannotReadOamFromLineStartToHBlank)
{
	// Mooneye-test-suite's lcdon_timing-GS
	MemoryMap.WriteByte(OamAddress, 0x12);
	TurnOn();

	for (auto position : { 0, 4, 80, 84, HBlankStart - 4 })
	{
		RunTo(1, position);
		EXPECT_EQ(0xff, MemoryMap.ReadByte(OamAddress)) << "at " << position;
	}

	RunTo(1, HBlankStart);
	EXPECT_EQ(0x12, MemoryMap.ReadByte(OamAddress));
}

TEST_F(PpuTimingTests, CpuCanWriteOamOnlyAtLineStartLastScanCycleAndHBlank)
{
	// Mooneye-test-suite's lcdon_write_timing-GS. Each position writes its own OAM byte
	const int positions[] = { 0, 4, 76, 80, 84, HBlankStart - 4, HBlankStart };
	const bool allowed[] = { true, false, false, true, false, false, true };

	TurnOn();
	for (unsigned short i = 0; i < 7; i++)
	{
		RunTo(1, positions[i]);
		MemoryMap.WriteByte(OamAddress + i, 1);
	}

	RunTo(Graphics::VertPixels, 8);
	for (unsigned short i = 0; i < 7; i++)
	{
		EXPECT_EQ(allowed[i] ? 1 : 0, MemoryMap.ReadByte(OamAddress + i)) << "written at " << positions[i];
	}
}

TEST_F(PpuTimingTests, CpuCannotReadVramFromLastScanCycleNorWriteItInMode3)
{
	// Mooneye-test-suite's lcdon_timing-GS and lcdon_write_timing-GS
	MemoryMap.WriteByte(VramAddress, 0x56);
	TurnOn();

	RunTo(1, 76);
	EXPECT_EQ(0x56, MemoryMap.ReadByte(VramAddress));

	RunTo(1, 80);
	EXPECT_EQ(0xff, MemoryMap.ReadByte(VramAddress));
	MemoryMap.WriteByte(VramAddress, 0x78);

	RunTo(1, 84);
	MemoryMap.WriteByte(VramAddress, 0x9a);
	EXPECT_EQ(0xff, MemoryMap.ReadByte(VramAddress));

	RunTo(1, HBlankStart);
	EXPECT_EQ(0x78, MemoryMap.ReadByte(VramAddress));
}

TEST_F(PpuTimingTests, CpuCanAccessVramAndOamOnFirstLineUntilMode3)
{
	TurnOn();
	RunTo(0, 80);

	MemoryMap.WriteByte(VramAddress, 0x11);
	MemoryMap.WriteByte(OamAddress, 0x22);
	EXPECT_EQ(0x11, MemoryMap.ReadByte(VramAddress));
	EXPECT_EQ(0x22, MemoryMap.ReadByte(OamAddress));
}

TEST_F(PpuTimingTests, CpuCanAccessVramAndOamInVBlankAndWhileLcdIsOff)
{
	MemoryMap.WriteByte(VramAddress, 0x11);
	MemoryMap.WriteByte(OamAddress, 0x22);
	EXPECT_EQ(0x11, MemoryMap.ReadByte(VramAddress));
	EXPECT_EQ(0x22, MemoryMap.ReadByte(OamAddress));

	TurnOn();
	RunTo(150, 100);
	MemoryMap.WriteByte(VramAddress, 0x33);
	MemoryMap.WriteByte(OamAddress, 0x44);
	EXPECT_EQ(0x33, MemoryMap.ReadByte(VramAddress));
	EXPECT_EQ(0x44, MemoryMap.ReadByte(OamAddress));
}

TEST_F(PpuTimingTests, UnusableAreaAfterOamReadsZeroOrFFWhileOamIsBlocked)
{
	EXPECT_EQ(0, MemoryMap.ReadByte(0xfea0));

	TurnOn();
	RunTo(1, 0);
	EXPECT_EQ(0xff, MemoryMap.ReadByte(0xfea0));
}

class Mode3LengthTests : public PpuTimingTests
{
public:
	// Positions of mode 0 on line 1 for the LCDC given, found by watching STAT
	int HBlankPosition(unsigned char lcdControl)
	{
		TurnOn(lcdControl);
		RunTo(1, TestGraphics::DrawStart);

		auto position = TestGraphics::DrawStart;
		for (; Mode() != LcdcStatus::HBlankMode; position += 4) Run(4);
		return position;
	}

	void SetSprite(int index, unsigned char x)
	{
		// On lines 0-7
		Graphics.WriteOamDirect(static_cast<unsigned short>(index * 4), 16);
		Graphics.WriteOamDirect(static_cast<unsigned short>(index * 4 + 1), x);
	}

	Mode3LengthTests()
	{
		// Every sprite off screen above
		for (unsigned short i = 0; i < Graphics::OamSize; i++) Graphics.WriteOamDirect(i, 0);
	}
};

TEST_F(Mode3LengthTests, Lasts172ClocksWithoutPenalties)
{
	EXPECT_EQ(TestGraphics::DrawStart + 172, HBlankPosition(0));
}

TEST_F(Mode3LengthTests, ScxLengthensItBySubTilePixels)
{
	// Mooneye-test-suite's hblank_ly_scx_timing-GS: in whole machine cycles, 1-4 pixels add one, 5-7 add two
	const int expectedExtraCycles[] = { 0, 1, 1, 1, 1, 2, 2, 2, 0 };

	for (auto scx = 0; scx <= 8; scx++)
	{
		WriteRegister(TestGraphics::RegBgScrollX, static_cast<unsigned char>(scx));
		EXPECT_EQ(HBlankStart + expectedExtraCycles[scx] * 4, HBlankPosition(0)) << "SCX " << scx;
	}
}

TEST_F(Mode3LengthTests, SpritesLengthenItPerPanDocsPenaltiesRoundedDownToMachineCycles)
{
	// Mooneye-test-suite's intr_2_mode0_timing_sprites, with penalties per
	// https://gbdev.io/pandocs/Rendering.html#obj-penalty-algorithm
	const unsigned char spritesOn = 0x02;

	// A sprite at X 0 costs 11 clocks: 2 machine cycles
	SetSprite(0, 0);
	EXPECT_EQ(HBlankStart + 8, HBlankPosition(spritesOn));

	// Ten at X 0: the first 11, the rest 6 each, 65 clocks: 16 machine cycles
	for (auto i = 1; i < 10; i++) SetSprite(i, 0);
	EXPECT_EQ(HBlankStart + 64, HBlankPosition(spritesOn));
}

TEST_F(Mode3LengthTests, SpritePenaltyDependsOnPositionInBackgroundTile)
{
	const unsigned char spritesOn = 0x02;

	// Leftmost pixel at screen X 4: 7 - 4 - 2 = 1 clock waiting for the tile, then 6: 7, one machine cycle
	SetSprite(0, 12);
	EXPECT_EQ(HBlankStart + 4, HBlankPosition(spritesOn));

	// A second sprite in the same tile only costs its 6: 13, three machine cycles
	SetSprite(1, 12);
	EXPECT_EQ(HBlankStart + 12, HBlankPosition(spritesOn));
}

TEST_F(Mode3LengthTests, SpritesOffScreenRightOrDisabledCostNothing)
{
	SetSprite(0, 168);
	EXPECT_EQ(HBlankStart, HBlankPosition(0x02));

	SetSprite(0, 8);
	EXPECT_EQ(HBlankStart, HBlankPosition(0));
}

TEST_F(Mode3LengthTests, WindowLengthensIt)
{
	// 6 clocks: a machine cycle
	WriteRegister(TestGraphics::RegWindowY, 0);
	WriteRegister(TestGraphics::RegWindowX, 7);

	EXPECT_EQ(HBlankStart + 4, HBlankPosition(0x20));
}

class PpuFrameTests : public PpuTimingTests
{
};

TEST_F(PpuFrameTests, FrameCompletesAsVBlankStarts)
{
	TurnOn();

	RunTo(Graphics::VertPixels, 0);
	EXPECT_FALSE(Graphics.TakeCompletedFrame());

	RunTo(Graphics::VertPixels, 4);
	EXPECT_TRUE(Graphics.TakeCompletedFrame());
	EXPECT_FALSE(Graphics.TakeCompletedFrame());

	Run(Graphics::FrameClocks - 4);
	EXPECT_FALSE(Graphics.TakeCompletedFrame());
	Run(4);
	EXPECT_TRUE(Graphics.TakeCompletedFrame());
}

TEST_F(PpuFrameTests, FramesCompleteEveryFrameClocksWhileLcdIsOff)
{
	for (auto frame = 0; frame < 3; frame++)
	{
		Run(Graphics::FrameClocks - 4);
		EXPECT_FALSE(Graphics.TakeCompletedFrame());
		Run(4);
		EXPECT_TRUE(Graphics.TakeCompletedFrame());
	}
}

TEST_F(PpuFrameTests, FramesKeepTheirCadenceWhenLcdIsTurnedOff)
{
	TurnOn();
	RunTo(Graphics::VertPixels, 4);
	ASSERT_TRUE(Graphics.TakeCompletedFrame());

	Run(1000);
	WriteRegister(TestGraphics::RegLcdControl, 0);
	Run(Graphics::FrameClocks - 1000 - 4);
	EXPECT_FALSE(Graphics.TakeCompletedFrame());
	Run(4);
	EXPECT_TRUE(Graphics.TakeCompletedFrame());
}

TEST_F(PpuFrameTests, FrameTheLcdIsTurnedOnInEndsAtItsFirstVBlank)
{
	// Line 0 starts 4 clocks in, so V-Blank (line 144, 4 clocks in) comes 144 lines after turning on
	const int clocksOffBeforeTurnOn = 30000;
	const int turnOnToVBlank = 144 * 456;

	Run(clocksOffBeforeTurnOn);
	TurnOn();
	Run(turnOnToVBlank - 4);
	EXPECT_FALSE(Graphics.TakeCompletedFrame());

	Run(4);
	EXPECT_TRUE(Graphics.TakeCompletedFrame());
}

TEST_F(PpuFrameTests, FirstFrameAfterTurningLcdOnIsBlank)
{
	// Per https://gbdev.io/pandocs/LCDC.html#lcdc7--lcd-enable. Background palette colour 0 is shade 3, not blank
	WriteRegister(TestGraphics::RegBgWinPalette, 0xff);
	TurnOn(0x01);

	RunTo(Graphics::VertPixels, 4);
	ASSERT_TRUE(Graphics.TakeCompletedFrame());
	EXPECT_EQ(-1, Graphics.Bitmap[0]);

	Run(Graphics::FrameClocks);
	ASSERT_TRUE(Graphics.TakeCompletedFrame());
	EXPECT_EQ(Graphics::ShadeColour(3), Graphics.Bitmap[0]);
}

TEST_F(PpuFrameTests, FrameWhileLcdIsOffIsBlank)
{
	DrawFirstLine(0x01);
	WriteRegister(TestGraphics::RegLcdControl, 0);
	Run(Graphics::FrameClocks);

	ASSERT_TRUE(Graphics.TakeCompletedFrame());
	for (auto pixel = 0u; pixel < Graphics::HozPixels * Graphics::VertPixels; pixel++)
	{
		ASSERT_EQ(-1, Graphics.Bitmap[pixel]) << "pixel " << pixel;
	}
}
