#pragma once
#include <string>
#include <vector>
#include "../core/MemoryMap.h"

// Reads what a ROM has drawn on a background tile map, for testing ROMs that report their results on screen
namespace ScreenText
{
	const unsigned short TileMap0 = 0x9800;
	const unsigned short TileMap1 = 0x9c00;
	const int TileMapWidth = 32;
	const int ScreenColumns = 20;
	const int ScreenRows = 18;

	// The tile numbers of the visible screen's rows, assuming the background isn't scrolled
	inline std::vector<std::vector<unsigned char>> ReadTiles(const MemoryMap& memory, unsigned short tileMap)
	{
		std::vector<std::vector<unsigned char>> rows(ScreenRows);

		for (auto row = 0; row < ScreenRows; row++)
		{
			for (auto column = 0; column < ScreenColumns; column++)
			{
				rows[row].push_back(memory.ReadByte(static_cast<unsigned short>(tileMap + row * TileMapWidth + column)));
			}
		}

		return rows;
	}

	// The screen as lines of text, with each tile converted by the ROM's character map
	template<typename CharMap>
	std::string ToText(const std::vector<std::vector<unsigned char>>& tiles, CharMap charMap)
	{
		std::string text;

		for (auto& row : tiles)
		{
			for (auto tile : row) text += charMap(tile);
			text += '\n';
		}

		return text;
	}
}
