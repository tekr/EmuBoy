#include "stdafx.h"
#include "Graphics.h"
#include "SpriteManager.h"
#include "CgbColourCorrection.h"
#include <algorithm>

// Drawing in CGB mode, per https://gbdev.io/pandocs/Tile_Maps.html#bg-map-attributes-cgb-mode-only,
// https://gbdev.io/pandocs/OAM.html#byte-3--attributesflags and
// https://gbdev.io/pandocs/Tile_Maps.html#bg-to-obj-priority-in-cgb-mode, checked against cgb-acid2

namespace
{
	// Background map attributes, in VRAM bank 1 alongside the tile numbers in bank 0
	const unsigned char BgPalette = 0x07;
	const unsigned char BgVramBank = 0x08;
	const unsigned char BgXFlip = 0x20;
	const unsigned char BgYFlip = 0x40;
	const unsigned char BgOverObjects = 0x80;
}

int Graphics::DrawCgbLine()
{
	auto windowVisibleThisLine = WindowEnabled() && _currentScanline >= _registers[RegWindowY] && _registers[RegWindowX] < 167;
	auto windowStartX = _registers[RegWindowX] - 7;

	if (_dmaDuringOamScan) _spriteManager.ClearVisibleSprites();
	else _spriteManager.SelectSprites(_currentScanline);

	unsigned char bgColours[HozPixels];
	unsigned char bgAttributes[HozPixels];
	DrawCgbBackground(bgColours, bgAttributes, windowVisibleThisLine, windowStartX);

	// Each pixel's object colour (0 where no object shows) and the attributes of the object it's from
	unsigned char objectColours[HozPixels]{};
	unsigned char objectFlags[HozPixels]{};
	if (SpritesEnabled()) DrawCgbObjects(objectColours, objectFlags);

	// LCDC bit 0 doesn't hide the background in CGB mode: clear, it gives objects priority over it
	auto masterPriority = BackgroundEnabled();
	auto& colourTable = CgbColourCorrection::Table();
	auto line = _currentScanline * HozPixels;

	for (auto x = 0u; x < HozPixels; x++)
	{
		auto objectShows = objectColours[x] != 0 &&
			(bgColours[x] == 0 || !masterPriority || !((bgAttributes[x] | objectFlags[x]) & BgOverObjects));

		auto colour = objectShows
			? _palettes.Colour(CgbPalettes::Set::Objects, objectFlags[x] & SpriteFlags::CgbPalette, objectColours[x])
			: _palettes.Colour(CgbPalettes::Set::Background, bgAttributes[x] & BgPalette, bgColours[x]);

		_cgbFrameRgb555[line + x] = colour;
		Bitmap[line + x] = colourTable[colour];
	}

	if (windowVisibleThisLine) ++_currentWindowScanline;

	return MinDrawClocks + _registers[RegBgScrollX] % 8 + DrawPenalty(windowVisibleThisLine, windowStartX);
}

void Graphics::DrawCgbBackground(unsigned char* colours, unsigned char* attributes, bool windowVisible, int windowStartX) const
{
	auto lcdControl = _registers[RegLcdControl];
	auto backgroundMap = lcdControl & 0x08 ? TileMapBase2 : TileMapBase1;
	auto windowMap = lcdControl & 0x40 ? TileMapBase2 : TileMapBase1;
	auto backgroundY = (_currentScanline + _registers[RegBgScrollY]) & 0xff;

	// A tile at a time: the window can start part-way through a background tile
	for (auto x = 0; x < static_cast<int>(HozPixels);)
	{
		auto inWindow = windowVisible && x >= windowStartX;
		auto mapX = inWindow ? x - windowStartX : (x + _registers[RegBgScrollX]) & 0xff;
		auto mapY = inWindow ? static_cast<int>(_currentWindowScanline) : backgroundY;

		auto mapAddress = (inWindow ? windowMap : backgroundMap) + GetTileOffset(mapX, mapY);
		auto tileNumber = _vram[mapAddress];
		auto tileAttributes = _vram[VramSize + mapAddress];

		// Tile numbers index $8000-$8FFF, or with LCDC bit 4 clear, $8800-$97FF counting from $9000
		auto tileAddress = lcdControl & 0x10
			? TileDataTableBase1 + tileNumber * 16
			: TileDataTableBase2 + static_cast<unsigned char>(tileNumber + 128) * 16;

		auto row = mapY & 7;
		if (tileAttributes & BgYFlip) row = 7 - row;

		auto rowAddress = (tileAttributes & BgVramBank ? VramSize : 0) + tileAddress + row * 2;
		auto low = _vram[rowAddress];
		auto high = _vram[rowAddress + 1];

		for (auto column = mapX & 7; column < 8 && x < static_cast<int>(HozPixels); column++, x++)
		{
			if (!inWindow && windowVisible && x == windowStartX) break;

			auto shift = tileAttributes & BgXFlip ? column : 7 - column;
			colours[x] = static_cast<unsigned char>((low >> shift & 1) | (high >> shift & 1) << 1);
			attributes[x] = tileAttributes;
		}
	}
}

void Graphics::DrawCgbObjects(unsigned char* colours, unsigned char* flags) const
{
	// The sprite manager lists the line's objects by X position, then OAM position. The CGB ranks them by OAM position
	// alone (see WriteCgbRegister for OPRI). The objects are SpriteData overlaying OAM, so their addresses are in OAM
	// order
	const SpriteData* objects[SpriteManager::MaxSpritesPerLine];
	auto count = 0;
	for (auto object : _spriteManager.GetVisibleSprites()) objects[count++] = object;

	auto byOamPosition = [](const SpriteData* a, const SpriteData* b) { return a < b; };
	std::sort(objects, objects + count, byOamPosition);

	// Drawn from the lowest priority up, so where objects overlap the highest-priority one's opaque pixels end up on
	// top, as the PPU picks the first object with an opaque pixel
	for (auto i = count - 1; i >= 0; i--)
	{
		auto& object = *objects[i];
		auto vram = _vram + (object.Flags & SpriteFlags::CgbVramBank ? VramSize : 0);

		for (auto column = 0; column < SpriteManager::SpriteWidth; column++)
		{
			auto x = object.XPos - SpriteManager::SpriteXOffset + column;
			if (x < 0 || x >= static_cast<int>(HozPixels)) continue;

			auto colour = _spriteManager.GetSpriteColour(object, x, _currentScanline, vram);
			if (colour == 0) continue;

			colours[x] = colour;
			flags[x] = object.Flags;
		}
	}
}
