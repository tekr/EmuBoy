#include "stdafx.h"
#include <gtest/gtest.h>
#include "GraphicsTestFixture.h"
#include "../core/OamDma.h"

// OAM DMA, per https://gbdev.io/pandocs/OAM_DMA_Transfer.html, with the timing measured by mooneye-test-suite's
// oam_dma_start, oam_dma_timing and oam_dma_restart (https://github.com/Gekkio/mooneye-test-suite)

namespace
{
	const unsigned short DmaPort = 0xff46;
	const unsigned short OamAddress = 0xfe00;
	const unsigned short WorkRam = 0xc000;
	const unsigned short HighRam = 0xff80;
	const unsigned short VramAddress = 0x8000;
	const int TransferCycles = 160;
}

class OamDmaTests : public GraphicsTestFixture
{
public:
	::OamDma OamDma{ MemoryMap, Graphics };

	OamDmaTests()
	{
		MemoryMap.SetOamDma(&OamDma);
		Graphics.SetOamDma(&OamDma);

		for (unsigned short i = 0; i < Graphics::OamSize; i++)
		{
			MemoryMap.WriteByte(WorkRam + i, static_cast<unsigned char>(i ^ 0x5a));
			MemoryMap.WriteByte(WorkRam + 0x100 + i, static_cast<unsigned char>(i ^ 0xa5));
		}
	}

	// Runs machine cycles of the DMA and the PPU, as the emulator does
	void RunCycles(int cycles)
	{
		for (auto i = 0; i < cycles; i++)
		{
			OamDma.RunCycle();
			Run(4);
		}
	}

	void Start(unsigned char source) { MemoryMap.WriteByte(DmaPort, source); }

	bool OamMatches(unsigned short source)
	{
		for (unsigned short i = 0; i < Graphics::OamSize; i++)
		{
			if (MemoryMap.ReadByte(OamAddress + i) != MemoryMap.ReadByte(source + i)) return false;
		}

		return true;
	}
};

TEST_F(OamDmaTests, TransferStartsTwoMachineCyclesAfterTheWrite)
{
	Start(WorkRam >> 8);

	RunCycles(1);
	EXPECT_FALSE(OamDma.IsTransferring());
	EXPECT_EQ(0, MemoryMap.ReadByte(OamAddress));

	RunCycles(1);
	EXPECT_TRUE(OamDma.IsTransferring());
	EXPECT_EQ(0xff, MemoryMap.ReadByte(OamAddress));
}

TEST_F(OamDmaTests, TransferTakes160MachineCycles)
{
	Start(WorkRam >> 8);

	RunCycles(1 + TransferCycles);
	EXPECT_TRUE(OamDma.IsTransferring());

	RunCycles(1);
	EXPECT_FALSE(OamDma.IsTransferring());
	EXPECT_TRUE(OamMatches(WorkRam));
}

TEST_F(OamDmaTests, CpuReadsFFFromOamDuringTransfer)
{
	Start(WorkRam >> 8);
	RunCycles(10);

	EXPECT_EQ(0xff, MemoryMap.ReadByte(OamAddress));
	EXPECT_EQ(0xff, MemoryMap.ReadByte(0xfea0));

	MemoryMap.WriteByte(OamAddress + 0x50, 0x12);
	RunCycles(TransferCycles);
	EXPECT_EQ((0x50 ^ 0x5a), MemoryMap.ReadByte(OamAddress + 0x50));
}

TEST_F(OamDmaTests, CpuReadOnSourceBusGetsByteBeingTransferred)
{
	MemoryMap.WriteByte(0xd000, 0x77);
	Start(WorkRam >> 8);
	RunCycles(1 + 5);

	// The fifth byte, from $C004
	EXPECT_EQ(4 ^ 0x5a, MemoryMap.ReadByte(0xd000));
	EXPECT_EQ(4 ^ 0x5a, MemoryMap.ReadByte(0x0000));

	RunCycles(TransferCycles);
	EXPECT_EQ(0x77, MemoryMap.ReadByte(0xd000));
}

TEST_F(OamDmaTests, CpuWritesToSourceBusAreLost)
{
	MemoryMap.WriteByte(0xd000, 0);
	Start(WorkRam >> 8);
	RunCycles(10);

	MemoryMap.WriteByte(0xd000, 0x77);
	RunCycles(TransferCycles);
	EXPECT_EQ(0, MemoryMap.ReadByte(0xd000));
}

TEST_F(OamDmaTests, CpuCanUseHighRamAndOtherBus)
{
	MemoryMap.WriteByte(VramAddress, 0x33);
	Start(WorkRam >> 8);
	RunCycles(10);

	MemoryMap.WriteByte(HighRam, 0x44);
	EXPECT_EQ(0x44, MemoryMap.ReadByte(HighRam));
	EXPECT_EQ(0x33, MemoryMap.ReadByte(VramAddress));
}

TEST_F(OamDmaTests, TransferFromVramBlocksVramButNotWorkRam)
{
	MemoryMap.WriteByte(VramAddress + 0x10, 0x33);
	MemoryMap.WriteByte(0xd000, 0x77);
	Start(VramAddress >> 8);
	RunCycles(10);

	EXPECT_EQ(0x77, MemoryMap.ReadByte(0xd000));
	EXPECT_NE(0x33, MemoryMap.ReadByte(VramAddress + 0x10));
}

TEST_F(OamDmaTests, RestartKeepsCurrentTransferGoingUntilTheNewOneStarts)
{
	// Mooneye-test-suite's oam_dma_start and oam_dma_restart
	Start(WorkRam >> 8);
	RunCycles(1 + 10);

	Start((WorkRam + 0x100) >> 8);
	RunCycles(1);
	EXPECT_TRUE(OamDma.IsTransferring());

	RunCycles(TransferCycles);
	EXPECT_TRUE(OamDma.IsTransferring());

	RunCycles(1);
	EXPECT_FALSE(OamDma.IsTransferring());
	EXPECT_TRUE(OamMatches(WorkRam + 0x100));
}

TEST_F(OamDmaTests, SourcesFromE000ReadWorkRam)
{
	// Mooneye-test-suite's oam_dma/sources-GS
	Start(0xe0);
	RunCycles(2 + TransferCycles);

	EXPECT_TRUE(OamMatches(WorkRam));
}

TEST_F(OamDmaTests, RegisterReadsBackLastValueWritten)
{
	Start(0xc1);
	EXPECT_EQ(0xc1, MemoryMap.ReadByte(DmaPort));
}

TEST_F(OamDmaTests, TransferDuringOamScanHidesSprites)
{
	// Per Pan Docs, the PPU reads every object as off screen while OAM DMA is running in mode 2. A solid sprite at
	// the left of lines 1-8 over a blank background, copied over itself
	const unsigned short lineWidth = Graphics::HozPixels;
	for (unsigned short i = 0; i < 16; i++) MemoryMap.WriteByte(VramAddress + i, 0xff);
	WriteRegister(TestGraphics::RegBgWinPalette, 0);
	WriteRegister(TestGraphics::RegSprite0Palette, 0xff);
	for (unsigned short i = 0; i < Graphics::OamSize; i++) MemoryMap.WriteByte(WorkRam + i, 0);
	MemoryMap.WriteByte(WorkRam, 17);
	MemoryMap.WriteByte(WorkRam + 1, 8);
	for (unsigned short i = 0; i < Graphics::OamSize; i++) Graphics.WriteOamDirect(i, MemoryMap.ReadByte(WorkRam + i));

	DrawFirstLine(0x02);
	RunTo(0, 300);
	Start(WorkRam >> 8);
	RunCycles((Graphics::ScanlineClocks - 300 + TestGraphics::DrawStart) / 4);

	EXPECT_TRUE(OamDma.IsTransferring());
	EXPECT_EQ(Graphics::ShadeColour(0), Graphics.Bitmap[lineWidth]);

	RunCycles(TransferCycles);
	EXPECT_FALSE(OamDma.IsTransferring());
	RunTo(3, TestGraphics::DrawStart);
	EXPECT_EQ(Graphics::ShadeColour(3), Graphics.Bitmap[3 * lineWidth]);
}
