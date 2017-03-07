#pragma once
#include "Graphics.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/composite_key.hpp>

using namespace boost::multi_index;

using VisibleSpriteContainer = multi_index_container<SpriteData*, indexed_by<
	ordered_unique<composite_key<SpriteData,
		member<SpriteData, uint8_t, &SpriteData::XPos>,
		const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>
	>>,
	hashed_unique<const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>>
>>;

class SpriteManager
{
	VisibleSpriteContainer _visibleSprites;

	multi_index_container<SpriteData*, indexed_by<
		ordered_non_unique<member<SpriteData, uint8_t, &SpriteData::YPos>>,
		hashed_unique<const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>>
	>> _yOrderedSprites;

	uint8_t _currentScanline;
	uint8_t _spriteHeight;

public:

	// GB hardware supports 10 sprites per scanline
	static const int MaxSpritesPerLine = 10;

	static const int SpriteWidth = 8;
	static const int NormalSpriteHeight = 8;
	static const int TallSpriteHeight = 16;

	static const int SpriteXOffset = 8;
	static const int SpriteYOffset = 16;


	SpriteManager();

	void SetUseTallSprites(bool tallSprites) { _spriteHeight = tallSprites ? TallSpriteHeight : NormalSpriteHeight; }

	const VisibleSpriteContainer& GetVisibleSprites() const { return _visibleSprites; }

	void SetScanline(uint8_t scanline);

	void NextScanline()	{ SetScanline(_currentScanline + 1); }

	void SpriteMoved(SpriteData& spriteData);

	uint8_t GetSpriteColour(SpriteData& spriteData, int x, int y, uint8_t* vram) const;
};

