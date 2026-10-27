#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include "TestGraphics.h"
#include <gtest/gtest.h>
#include "../core/SpriteManager.h"

class InputJoypad;

class GraphicsTestFixture : public testing::Test
{
public:
	InputJoypad Joypad;
	TestMemoryMap Memory { Joypad };
	TestCpu Processor{ Memory };
	SpriteManager Sprites{};
	TestGraphics Ppu{ Processor, Memory, Sprites };

	static const unsigned char DisplayOn = 0x80;

	GraphicsTestFixture() {}
	~GraphicsTestFixture();

	unsigned char ReadRegister(unsigned short reg) { return Memory.ReadByte(MemoryMap::VramRegisters + reg); }
	void WriteRegister(unsigned short reg, unsigned char value) { Memory.WriteByte(MemoryMap::VramRegisters + reg, value); }

	// Runs the PPU a machine cycle at a time, as the emulator does
	void Run(int clocks);

	// Turns the LCD on (with the other LCDC bits given), which starts line 0 at FirstLineStart
	void TurnOn(unsigned char lcdControl = 0);

	// Runs from turning the LCD on to the given position (clocks since LY changed) in the given line of the first
	// frame. Positions only move forwards
	void RunTo(unsigned int line, int position);

	// Turns the LCD on and runs until line 0 has been drawn, as its mode 3 starts
	void DrawFirstLine(unsigned char lcdControl);

	// Runs a line, so the next line is drawn
	void DrawNextLine() { Run(Graphics::ScanlineClocks); }

private:
	// Clocks run since the LCD was turned on
	int _clocksSinceTurnOn{ 0 };
};
