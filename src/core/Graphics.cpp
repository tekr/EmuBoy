#include "stdafx.h"
#include "Graphics.h"
#include "MemoryMap.h"
#include "Cpu.h"
#include "SpriteManager.h"

unsigned char Graphics::GetColour(int x, int y, TileType tileType) const
{
	auto tileMapBase = _registers[RegLcdControl] & (tileType == TileType::Window ? 0x40 : 0x8)
		                   ? TileMapBase2
		                   : TileMapBase1;

	auto tileNumber = _vram[tileMapBase + GetTileOffset(x, y)];

	unsigned short tileDataBase;

	if (_registers[RegLcdControl] & 0x10)
	{
		tileDataBase = TileDataTableBase1;
	}
	else
	{
		tileDataBase = TileDataTableBase2;
		tileNumber += 128;
	}

	auto baseByte = tileDataBase + (tileNumber * 16) + ((y & 0x7) << 1);
	auto bitShift = 7 - x % 8;

	return _vram[baseByte] >> bitShift & 0x1 | (_vram[baseByte + 1] >> bitShift & 0x1) << 1;
}

unsigned char Graphics::GetSpriteColour(SpriteData& spriteData, int x, int y) const
{
	auto patternX = x - spriteData.XPos + 8;
	auto patternY = y - spriteData.YPos + 16;

	if (spriteData.Flags & SpriteFlags::XFlip) patternX = 7 - patternX;
	if (spriteData.Flags & SpriteFlags::YFlip) patternY = _spriteManager.GetSpriteHeight() - 1 - patternY;

	auto baseByte = TileDataTableBase1 + (spriteData.PatternNum * 16) + (patternY << 1);
	auto bitShift = 7 - patternX % 8;

	return _vram[baseByte] >> bitShift & 0x1 | (_vram[baseByte + 1] >> bitShift & 0x1) << 1;
}

int Graphics::MapColour(unsigned char colour, Palette palette)
{
	auto paletteData = _registers[palette == Palette::BgAndWindow
		                              ? RegBgWinPalette
		                              : palette == Palette::Sprite0
										? RegSprite0Palette
										: RegSprite1Palette];

	// 50% opacity to emulate the slow GB screen response time
	// Yellow-green tinge to look more like the original
	return 0x80000000 | ((3 - (paletteData >> (colour << 1) & 0x3)) * 0x40504a);
}

void Graphics::CheckLineCompare()
{
	auto status = _registers[RegLcdStatus] & 0xfb | (DisplayEnabled() && _currentScanline == _registers[RegLineCompare] ? 0x4 : 0x0);
	_registers[RegLcdStatus] = status;

	if ((status & 0x44) == 0x44)
	{
		_cpu.RequestInterrupt(InterruptFlags::LcdStat);
	}
}

Graphics::Graphics(Cpu& cpu, MemoryMap& memoryMap, SpriteManager& spriteManager)
	: _cpu(cpu), _memoryMap(memoryMap), _screenEnabled(true), _totalCycles(0), _spriteManager(spriteManager)
{
	_memoryMap.SetGraphics(this);

	_registers[RegBgWinPalette] = 0xff;
	_registers[RegSprite0Palette] = 0xff;
	_registers[RegSprite1Palette] = 0xff;
}

Graphics::~Graphics()
{
}

void Graphics::WriteOam(unsigned short address, unsigned char value)
{
	if (_status != LcdcStatus::OamReadMode && _status != LcdcStatus::OamAndVramReadMode)
	{
		_oam[address] = value;

		// Check for change to X or Y coordinate
		if (address % 4 < 2)
		{
			auto pointer = reinterpret_cast<SpriteData*>(&_oam[address & 0xfffc]);
			_spriteManager.SpriteMoved(*pointer);
		}
	}
}

void Graphics::WriteRegister(unsigned short address, unsigned char value)
{
	// Writing to the line count register resets it
	if (address == RegLineCount) value = 0;
	else if (address == RegLcdControl) _spriteManager.SetUseTallSprites(value & 0x4);

	_registers[address] = value;
}

int Graphics::RenderLine()
{
	// Reset line counter at start of frame
	if (_currentScanline == 0)
	{
		_registers[RegLineCount] = 0;
		_spriteManager.SetScanline(0);
		CheckLineCompare();
	}
	else
	{
		_spriteManager.NextScanline();
	}

	// Reset window current line when the window start is reached
	if (_currentScanline == _registers[RegWindowY]) _currentWindowScanline = 0;

	auto windowWasEnabled = false;

	if (_currentScanline < VertPixels)
	{
		// According to docs, should only change during VBlank
		if (DisplayEnabled())
		{
			// A very inefficient rendering implementation. Does many
			// calls/recalculates many intermediate values for every pixel.

			auto backgroundY = _currentScanline + _registers[RegBgScrollY];
			auto windowY = _currentWindowScanline;
			auto belowWindowStart = WindowEnabled() && _currentScanline >= _registers[RegWindowY];

			auto sprites = _spriteManager.GetActiveSprites();
			auto spriteIter = sprites.cbegin();
			auto spriteIterEnd = sprites.cend();

			auto spritesThisLine = 0;

			auto backgroundEnabled = BackgroundEnabled();
			auto spritesEnabled = SpritesEnabled();

			for (auto x = 0; x < HozPixels;x++)
			{
				auto windowX = x - _registers[RegWindowX] + 7;
				auto& pixel = Bitmap[_currentScanline*HozPixels + x];

				unsigned char colour = 0;

				if (belowWindowStart && windowX >= 0)
				{
					// Window is on top
					colour = GetColour(windowX, windowY, TileType::Window);
					windowWasEnabled = true;
				}
				else if (backgroundEnabled)
				{
					auto backgroundX = x + _registers[RegBgScrollX];
					colour = GetColour(backgroundX, backgroundY, TileType::Background);
				}

				if (spritesEnabled)
				{
					// Sprite x-offset is -8 and width is +8; the two cancel out
					while (spriteIter != spriteIterEnd && (*spriteIter)->XPos <= x)
					{
						++spriteIter;
						++spritesThisLine;
					}

					if (spriteIter != spriteIterEnd && spritesThisLine <= SpriteManager::MaxSpritesPerLine &&
						// Don't draw sprite unless it's visible over Window & BG (based on priority and BG/Window colour)
						(!((*spriteIter)->Flags & SpriteFlags::ZPriority) || colour != 0))
					{
						colour = GetSpriteColour(**spriteIter, x, _currentScanline);
						pixel = MapColour(colour, (*spriteIter)->Flags & SpriteFlags::PaletteSelector ? Palette::Sprite1 : Palette::Sprite0);
					}
					else
					{
						pixel = MapColour(colour, Palette::BgAndWindow);
					}
				}
				else
				{
					pixel = MapColour(colour, Palette::BgAndWindow);
				}
			}
		}
		else
		{
			memset(&Bitmap[_currentScanline*HozPixels], 0x2f, HozPixels*4);
		}
	}

	_registers[RegLineCount]++;
	_currentScanline++;

	// Must only increase window scanline counter if it was enabled for this line
	if (windowWasEnabled) _currentWindowScanline++;

	CheckLineCompare();
	return ScanlineClocks;
}

void Graphics::SetLcdcStatus(LcdcStatus status)
{
	_status = DisplayEnabled() ? status : LcdcStatus::VBlankMode;
	_registers[RegLcdStatus] = (_registers[RegLcdStatus] & 0xfc) | status;

	if (!DisplayEnabled()) return;

	unsigned char interruptMask = 0;

	switch (status)
	{
	case LcdcStatus::OamReadMode:
		interruptMask = 0x20;
		break;

	case LcdcStatus::HBlankMode:
		interruptMask = 0x8;
		break;

	case LcdcStatus::VBlankMode:
		// According to http://gameboy.mongenel.com/dmg/istat98.txt, this mode fires the LCDC
		// interrupt if the OAM interrupt is enabled
		interruptMask = 0x30;

		_cpu.RequestInterrupt(InterruptFlags::VBlank);
		break;
	}

	if (_registers[RegLcdStatus] & interruptMask)
	{
		_cpu.RequestInterrupt(InterruptFlags::LcdStat);
	}
}
