#pragma once
#include "../core/Graphics.h"

class TestGraphics : public Graphics
{
public:
	using Graphics::GetBgOrWinColour;
	using Graphics::MapColour;
	
	using Graphics::DisplayEnabled;
	using Graphics::WindowEnabled;
	using Graphics::BackgroundEnabled;
	using Graphics::SpritesEnabled;
	using Graphics::GetTileOffset;

	TestGraphics(Cpu& cpu, MemoryMap& memoryMap, SpriteManager& spriteManager) : Graphics(cpu, memoryMap, spriteManager)
	{
	}

	~TestGraphics() = default;
};

