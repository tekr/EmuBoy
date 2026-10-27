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
	OamDma OamDmaController{ Memory, Ppu };

	OamDmaTests()
	{
		Memory.SetOamDma(&OamDmaController);
		Ppu.SetOamDma(&OamDmaController);

		for (unsigned short i = 0; i < Graphics::OamSize; i++)
		{
			Memory.WriteByte(WorkRam + i, static_cast<unsigned char>(i ^ 0x5a));
			Memory.WriteByte(WorkRam + 0x100 + i, static_cast<unsigned char>(i ^ 0xa5));
		}
	}

	// Runs machine cycles of the DMA and the PPU, as the emulator does
	void RunCycles(int cycles)
	{
		for (auto i = 0; i < cycles; i++)
		{
			OamDmaController.RunCycle();
			Run(4);
		}
	}

	void Start(unsigned char source) { Memory.WriteByte(DmaPort, source); }

	bool OamMatches(unsigned short source)
	{
		for (unsigned short i = 0; i < Graphics::OamSize; i++)
		{
			if (Memory.ReadByte(OamAddress + i) != Memory.ReadByte(source + i)) return false;
		}

		return true;
	}
};

TEST_F(OamDmaTests, TransferStartsTwoMachineCyclesAfterTheWrite)
{
	Start(WorkRam >> 8);

	RunCycles(1);
	EXPECT_FALSE(OamDmaController.IsTransferring());
	EXPECT_EQ(0, Memory.ReadByte(OamAddress));

	RunCycles(1);
	EXPECT_TRUE(OamDmaController.IsTransferring());
	EXPECT_EQ(0xff, Memory.ReadByte(OamAddress));
}

TEST_F(OamDmaTests, TransferTakes160MachineCycles)
{
	Start(WorkRam >> 8);

	RunCycles(1 + TransferCycles);
	EXPECT_TRUE(OamDmaController.IsTransferring());

	RunCycles(1);
	EXPECT_FALSE(OamDmaController.IsTransferring());
	EXPECT_TRUE(OamMatches(WorkRam));
}

TEST_F(OamDmaTests, CpuReadsFFFromOamDuringTransfer)
{
	Start(WorkRam >> 8);
	RunCycles(10);

	EXPECT_EQ(0xff, Memory.ReadByte(OamAddress));
	EXPECT_EQ(0xff, Memory.ReadByte(0xfea0));

	Memory.WriteByte(OamAddress + 0x50, 0x12);
	RunCycles(TransferCycles);
	EXPECT_EQ((0x50 ^ 0x5a), Memory.ReadByte(OamAddress + 0x50));
}

TEST_F(OamDmaTests, CpuReadOnSourceBusGetsByteBeingTransferred)
{
	Memory.WriteByte(0xd000, 0x77);
	Start(WorkRam >> 8);
	RunCycles(1 + 5);

	// The fifth byte, from $C004
	EXPECT_EQ(4 ^ 0x5a, Memory.ReadByte(0xd000));
	EXPECT_EQ(4 ^ 0x5a, Memory.ReadByte(0x0000));

	RunCycles(TransferCycles);
	EXPECT_EQ(0x77, Memory.ReadByte(0xd000));
}

TEST_F(OamDmaTests, CpuWritesToSourceBusAreLost)
{
	Memory.WriteByte(0xd000, 0);
	Start(WorkRam >> 8);
	RunCycles(10);

	Memory.WriteByte(0xd000, 0x77);
	RunCycles(TransferCycles);
	EXPECT_EQ(0, Memory.ReadByte(0xd000));
}

TEST_F(OamDmaTests, CpuCanUseHighRamAndOtherBus)
{
	Memory.WriteByte(VramAddress, 0x33);
	Start(WorkRam >> 8);
	RunCycles(10);

	Memory.WriteByte(HighRam, 0x44);
	EXPECT_EQ(0x44, Memory.ReadByte(HighRam));
	EXPECT_EQ(0x33, Memory.ReadByte(VramAddress));
}

TEST_F(OamDmaTests, TransferFromVramBlocksVramButNotWorkRam)
{
	Memory.WriteByte(VramAddress + 0x10, 0x33);
	Memory.WriteByte(0xd000, 0x77);
	Start(VramAddress >> 8);
	RunCycles(10);

	EXPECT_EQ(0x77, Memory.ReadByte(0xd000));
	EXPECT_NE(0x33, Memory.ReadByte(VramAddress + 0x10));
}

TEST_F(OamDmaTests, RestartKeepsCurrentTransferGoingUntilTheNewOneStarts)
{
	// Mooneye-test-suite's oam_dma_start and oam_dma_restart
	Start(WorkRam >> 8);
	RunCycles(1 + 10);

	Start((WorkRam + 0x100) >> 8);
	RunCycles(1);
	EXPECT_TRUE(OamDmaController.IsTransferring());

	RunCycles(TransferCycles);
	EXPECT_TRUE(OamDmaController.IsTransferring());

	RunCycles(1);
	EXPECT_FALSE(OamDmaController.IsTransferring());
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
	EXPECT_EQ(0xc1, Memory.ReadByte(DmaPort));
}

TEST_F(OamDmaTests, TransferDuringOamScanHidesSprites)
{
	// Per Pan Docs, the PPU reads every object as off screen while OAM DMA is running in mode 2. A solid sprite at
	// the left of lines 1-8 over a blank background, copied over itself
	const unsigned short lineWidth = Graphics::HozPixels;
	for (unsigned short i = 0; i < 16; i++) Memory.WriteByte(VramAddress + i, 0xff);
	WriteRegister(TestGraphics::RegBgWinPalette, 0);
	WriteRegister(TestGraphics::RegSprite0Palette, 0xff);
	for (unsigned short i = 0; i < Graphics::OamSize; i++) Memory.WriteByte(WorkRam + i, 0);
	Memory.WriteByte(WorkRam, 17);
	Memory.WriteByte(WorkRam + 1, 8);
	for (unsigned short i = 0; i < Graphics::OamSize; i++) Ppu.WriteOamDirect(i, Memory.ReadByte(WorkRam + i));

	DrawFirstLine(0x02);
	RunTo(0, 300);
	Start(WorkRam >> 8);
	RunCycles((Graphics::ScanlineClocks - 300 + TestGraphics::DrawStart) / 4);

	EXPECT_TRUE(OamDmaController.IsTransferring());
	EXPECT_EQ(Graphics::ShadeColour(0), Ppu.Bitmap[lineWidth]);

	RunCycles(TransferCycles);
	EXPECT_FALSE(OamDmaController.IsTransferring());
	RunTo(3, TestGraphics::DrawStart);
	EXPECT_EQ(Graphics::ShadeColour(3), Ppu.Bitmap[3 * lineWidth]);
}
