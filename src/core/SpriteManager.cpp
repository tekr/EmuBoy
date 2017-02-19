#include "stdafx.h"
#include "SpriteManager.h"


SpriteManager::SpriteManager(): _currentScanline(0), _spriteHeight(NormalSpriteHeight)
{
}

void SpriteManager::SetScanline(unsigned char scanline)
{
	// TODO: Test for excessive memory allocation/freeing in this method

	_currentScanline = scanline;
	_visibleSprites.clear();

	auto firstSpriteNotAbove = _yOrderedSprites.upper_bound(_currentScanline + SpriteYOffset - _spriteHeight);
	auto firstSpriteBelow = _yOrderedSprites.upper_bound(_currentScanline + SpriteYOffset);

	for (auto sprite = firstSpriteNotAbove; sprite != firstSpriteBelow; ++sprite)
	{
		_visibleSprites.insert(*sprite);
	}
}

void SpriteManager::SpriteMoved(SpriteData& spriteData)
{
	_visibleSprites.get<1>().erase(spriteData.OrderedSpriteId());
	_yOrderedSprites.get<1>().erase(spriteData.OrderedSpriteId());

	_yOrderedSprites.insert(&spriteData);
}

unsigned char SpriteManager::GetSpriteColour(SpriteData& spriteData, int x, int y, unsigned char* vram) const
{
	auto patternX = x - spriteData.XPos + SpriteXOffset;
	auto patternY = y - spriteData.YPos + SpriteYOffset;

	if (spriteData.Flags & SpriteFlags::XFlip) patternX = SpriteWidth - 1 - patternX;
	if (spriteData.Flags & SpriteFlags::YFlip) patternY = _spriteHeight - 1 - patternY;

	auto patternNum = spriteData.PatternNum;

	// Massage pattern number if using tall sprites
	if (_spriteHeight == TallSpriteHeight)
	{
		patternNum = patternNum & 0xfe | (patternY & 0x8) >> 3;
		patternY &= 0x7;
	}

	auto baseByte = Graphics::SpriteDataTableBase + patternNum * 16 + (patternY << 1);
	auto bitShift = 7 - patternX % 8;

	return vram[baseByte] >> bitShift & 0x1 | (vram[baseByte + 1] >> bitShift & 0x1) << 1;
}

SpriteManager::~SpriteManager()
{
}
