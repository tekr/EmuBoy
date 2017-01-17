#pragma once
#include "D:/Trading/src/EmuBoy/src/core/Graphics.h"

class TestGraphics : public Graphics
{
public:
	using Graphics::GetColour;
	using Graphics::MapColour;
	
	using Graphics::DisplayEnabled;
	using Graphics::WindowEnabled;
	using Graphics::BackgroundEnabled;
	using Graphics::SpritesEnabled;
	using Graphics::GetTileOffset;

	TestGraphics(Cpu& cpu, MemoryMap& memoryMap) : Graphics(cpu, memoryMap)
	{
	}

	~TestGraphics() = default;
};

