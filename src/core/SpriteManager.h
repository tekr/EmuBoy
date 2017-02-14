#pragma once
#include "Graphics.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/composite_key.hpp>

using namespace boost::multi_index;

class SpriteManager
{
	multi_index_container<SpriteData*, indexed_by<
		ordered_unique<composite_key<SpriteData,
			member<SpriteData, unsigned char, &SpriteData::XPos>,
			const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>
		>>,
		hashed_unique<const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>>
	>> _activeSprites;

	multi_index_container<SpriteData*, indexed_by<
		ordered_non_unique<member<SpriteData, unsigned char, &SpriteData::YPos>>,
		hashed_unique<const_mem_fun<SpriteData, uintptr_t, &SpriteData::OrderedSpriteId>>
	>> _yOrderedSprites;

	Graphics& _graphics;

	bool IsActive(SpriteData& spriteData) const
	{
		auto lineStart = spriteData.YPos - 16;
		return _graphics._currentScanline >= lineStart && _graphics._currentScanline < lineStart
				+ (_graphics.ReadRegister(Graphics::RegLcdControl) & 0x4 ? 16 : 8);
	}

public:


	explicit SpriteManager(Graphics& graphics)
		: _graphics(graphics)
	{
	}

	void RemoveSprite(SpriteData& spriteData)
	{
		_activeSprites.get<1>().erase(spriteData.OrderedSpriteId());
		_yOrderedSprites.get<1>().erase(spriteData.OrderedSpriteId());
	}

	void AddSprite(SpriteData& spriteData)
	{
		_yOrderedSprites.insert(&spriteData);

		if (IsActive(spriteData))
		{
			_activeSprites.insert(&spriteData);
		}
	}

	SpriteManager();
	~SpriteManager();
};

