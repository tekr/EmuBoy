#include "stdafx.h"
#include "GraphicsTestFixture.h"

GraphicsTestFixture::~GraphicsTestFixture()
{
}

void GraphicsTestFixture::Run(int clocks)
{
	ASSERT_EQ(0, clocks % 4) << "the PPU runs in machine cycles";

	for (; clocks > 0; clocks -= 4)
	{
		Ppu.RunCycles(4);
		_clocksSinceTurnOn += 4;
	}
}

void GraphicsTestFixture::TurnOn(unsigned char lcdControl)
{
	// Off first, so that the PPU restarts if it was already on
	WriteRegister(TestGraphics::RegLcdControl, lcdControl & ~DisplayOn);
	WriteRegister(TestGraphics::RegLcdControl, lcdControl | DisplayOn);
	_clocksSinceTurnOn = 0;
}

void GraphicsTestFixture::RunTo(unsigned int line, int position)
{
	// Line 0 starts part-way in when the LCD is turned on
	auto target = static_cast<int>(line * Graphics::ScanlineClocks) + position - TestGraphics::FirstLineStart;
	Run(target - _clocksSinceTurnOn);
}

void GraphicsTestFixture::DrawFirstLine(unsigned char lcdControl)
{
	TurnOn(lcdControl);
	RunTo(0, TestGraphics::DrawStart);
}
