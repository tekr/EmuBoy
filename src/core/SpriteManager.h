#pragma once
#include "Graphics.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/composite_key.hpp>

using namespace boost::multi_index;

using ActiveSpriteCollection = multi_index_container<SpriteData*, indexed_by<
	ordered_unique<composite_key<SpriteData,
	member<SpriteData, unsigned char, &SpriteData::XPos>,
	const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>
	>>,
	hashed_unique<const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>>
	>>;

class SpriteManager
{
	ActiveSpriteCollection _activeSprites;

	multi_index_container<SpriteData*, indexed_by<
		ordered_non_unique<member<SpriteData, unsigned char, &SpriteData::YPos>>,
		hashed_unique<const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>>
	>> _yOrderedSprites;

	unsigned char _currentScanline;
	unsigned char _spriteHeight;

	bool IsActive(SpriteData& spriteData) const
	{
		auto lineStart = spriteData.YPos - 16;
		return _currentScanline >= lineStart && _currentScanline < lineStart + _spriteHeight;
	}

public:

	// GB hardware supports 10 sprites per scanline
	static const int MaxSpritesPerLine = 10;

	SpriteManager();

	void SetUseTallSprites(bool tallSprites) { _spriteHeight = tallSprites ? 16 : 8; }

	unsigned char GetSpriteHeight() const { return _spriteHeight; }

	int GetLowerScanlineBound() const { return _currentScanline + 16 - _spriteHeight + 1; }

	int GetUpperScanlineBound() const {	return _currentScanline + 16; }

	const ActiveSpriteCollection& GetActiveSprites() const { return _activeSprites; }

	void SetScanline(unsigned char scanline)
	{
		_currentScanline = scanline;
		_activeSprites.clear();

		auto endIter = _yOrderedSprites.upper_bound(GetUpperScanlineBound());

		for (auto iter = _yOrderedSprites.lower_bound(GetLowerScanlineBound()); iter != endIter; ++iter)
		{
			_activeSprites.insert(*iter);
		}
	}


	void NextScanline()
	{
		SetScanline(_currentScanline + 1);
	}

	void SpriteMoved(SpriteData& spriteData)
	{
		_activeSprites.get<1>().erase(spriteData.OrderedSpriteId());
		_yOrderedSprites.get<1>().erase(spriteData.OrderedSpriteId());

		_yOrderedSprites.insert(&spriteData);

		if (IsActive(spriteData))
		{
			_activeSprites.insert(&spriteData);
		}
	}

	~SpriteManager();
};

