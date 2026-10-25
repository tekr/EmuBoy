
#include "stdafx.h"
#include <gtest/gtest.h>
#include "GraphicsTestFixture.h"
#include "TestGraphics.h"
#include "../core/CartridgeFactory.h"

void RunCpu(Cpu& cpu, int& currentCycle, int cycleTarget)
{
	while (currentCycle < cycleTarget && cpu.IsClockRunning())
	{
		currentCycle += cpu.DoNextInstruction();
	}

	currentCycle = std::max(currentCycle, cycleTarget);
}

TEST_F(GraphicsTestFixture, RunInternalRom)
{
	auto& reg = Cpu.Registers();

	reg.PC = 0;
	reg.SP = 0xffff;

	MemoryMap.SetInternalRomEnabled(true);

	auto cartridge = CartridgeFactory::LoadFromFile("../../ROMs/gb-snake.gb", 0);
	MemoryMap.SetCartridge(cartridge);

	auto frameCounter = 0;

	// Keep going until we're beyond the start of cartridge ROM
	while (reg.PC < 0x100)
	{
		Graphics.ResetFrame();
		auto cycleCounter = 0;
		auto cycleTarget = 0;

		for (auto i = 0; i < Graphics::VertPixels; i++)
		{
			Graphics.SetLcdcStatus(LcdcStatus::OamReadMode);
			cycleTarget += Graphics::OamReadClocks;
			RunCpu(Cpu, cycleCounter, cycleTarget);

			Graphics.SetLcdcStatus(LcdcStatus::OamAndVramReadMode);
			cycleTarget += Graphics::OamAndVramReadClocks;
			RunCpu(Cpu, cycleCounter, cycleTarget);

			Graphics.SetLcdcStatus(LcdcStatus::HBlankMode);
			cycleTarget += Graphics::HBlankPeriodClocks;
			RunCpu(Cpu, cycleCounter, cycleTarget);

			Graphics.RenderLine();
		}

		frameCounter++;

		Graphics.SetLcdcStatus(LcdcStatus::VBlankMode);

		for (auto i = 0; i < Graphics::VBlankLines; i++)
		{
			cycleTarget += Graphics::ScanlineClocks;
			RunCpu(Cpu, cycleCounter, cycleTarget);

			Graphics.RenderLine();
		}
	}
}

TEST_F(GraphicsTestFixture, WindowXRegisterKeepsWrittenValue)
{
	const unsigned char windowX = 0x57;

	Graphics.ResetFrame();
	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX, windowX);

	for (auto i = 0; i < 3; i++) Graphics.RenderLine();

	EXPECT_EQ(windowX, MemoryMap.ReadByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX));
}

TEST_F(GraphicsTestFixture, WindowDrawnFromWindowXPosition)
{
	const unsigned short tileBytes = 16;
	const unsigned short tileMapBytes = 32 * 32;
	const unsigned short windowTileMap = 0x9c00;
	const unsigned char identityPalette = 0xe4;
	const unsigned char windowTile = 1;
	const unsigned char allPixelsColour3 = 0xff;
	const auto windowLeftEdge = 80;
	const auto windowXOffset = 7;

	// Background is left as tile 0 (all colour 0) in map 0x9800
	for (unsigned short i = 0; i < tileBytes; i++) MemoryMap.WriteByte(MemoryMap::RamVideo + windowTile * tileBytes + i, allPixelsColour3);
	for (unsigned short i = 0; i < tileMapBytes; i++) MemoryMap.WriteByte(windowTileMap + i, windowTile);

	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgScrollY, 0);
	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgScrollX, 0);
	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegBgWinPalette, identityPalette);
	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowY, 0);
	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegWindowX, windowLeftEdge + windowXOffset);

	// Display, window (map at windowTileMap), tile data at RamVideo, background
	MemoryMap.WriteByte(MemoryMap::VramRegisters + TestGraphics::RegLcdControl, 0x80 | 0x40 | 0x20 | 0x10 | 0x01);

	Graphics.ResetFrame();
	Graphics.RenderLine();

	auto background = Graphics.Bitmap[0];
	auto window = Graphics.Bitmap[Graphics::HozPixels - 1];

	ASSERT_NE(background, window);

	for (auto x = 0; x < Graphics::HozPixels; x++)
	{
		EXPECT_EQ(x < windowLeftEdge ? background : window, Graphics.Bitmap[x]) << "at x = " << x;
	}
}
