#include "stdafx.h"
#include "Graphics.h"
#include "MemoryMap.h"
#include "Cpu.h"
#include "SpriteManager.h"

unsigned char Graphics::GetBgOrWinColour(int x, int y, TileType tileType) const
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
		_cpu.RequestInterrupt(InterruptFlags::LcdStatInt);
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
		auto locationChanged = address % 4 < 2 && value != _oam[address];
		_oam[address] = value;

		if (locationChanged)
		{
			auto spriteData = reinterpret_cast<SpriteData*>(&_oam[address & 0xfffc]);
			_spriteManager.SpriteMoved(*spriteData);
		}
	}
}

void Graphics::WriteRegister(unsigned short address, unsigned char value)
{
	// Writing to the line count register resets it
	if (address == RegLineCount) value = 0;
	else if (address == RegLcdControl) _spriteManager.SetUseTallSprites(value & 0x4);
	else if (address == RegDmaTransfer)
	{
		unsigned short sourceAddress = value << 8;
		unsigned short destAddress = 0;

		// Emulate DMA transfer
		for (auto i = 0; i < OamSize; i++)
		{
			WriteOam(destAddress++, _memoryMap.ReadByte(sourceAddress++));
		}
	}

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

	// Window current line needs to be reset when window start is reached
	if (_currentScanline == _registers[RegWindowY]) _currentWindowScanline = 0;

	if (_currentScanline < VertPixels)
	{
		if (DisplayEnabled())
		{
			// A rather inefficient rendering implementation.
			// Many intermediate results could be lifted outside loops

			auto backgroundY = _currentScanline + _registers[RegBgScrollY];
			auto belowWindowStart = WindowEnabled() && _currentScanline >= _registers[RegWindowY];

			auto visibleSprites = _spriteManager.GetVisibleSprites();
			auto sprite = visibleSprites.cbegin();
			auto firstNonvisibleSprite = visibleSprites.cend();

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
					colour = GetBgOrWinColour(windowX, _currentWindowScanline++, TileType::Window);
				}
				else if (backgroundEnabled)
				{
					auto backgroundX = x + _registers[RegBgScrollX];
					colour = GetBgOrWinColour(backgroundX, backgroundY, TileType::Background);
				}

				auto spritePixelOnTop = false;

				if (spritesEnabled)
				{
					while (sprite != firstNonvisibleSprite && (*sprite)->XPos - SpriteManager::SpriteXOffset + SpriteManager::SpriteWidth <= x)
					{
						++sprite;
						++spritesThisLine;
					}

					if (sprite != firstNonvisibleSprite && (*sprite)->XPos - SpriteManager::SpriteXOffset <= x && spritesThisLine <= SpriteManager::MaxSpritesPerLine &&
						// Don't draw sprite unless it's visible over Window & BG (based on priority and BG/Window colour)
						(!((*sprite)->Flags & SpriteFlags::ZPriority) || colour != 0))
					{
						auto spriteColour = _spriteManager.GetSpriteColour(**sprite, x, _currentScanline, _vram);
						spritePixelOnTop = spriteColour != 0;

						if (spritePixelOnTop)
						{
							pixel = MapColour(spriteColour, (*sprite)->Flags & SpriteFlags::PaletteSelector ? Palette::Sprite1 : Palette::Sprite0);
						}
					}
				}

				if (!spritePixelOnTop)
				{
					pixel = MapColour(colour, Palette::BgAndWindow);
				}
			}
		}
		else
		{
			memset(&Bitmap[_currentScanline*HozPixels], 0xff, HozPixels*4);
		}
	}

	_registers[RegLineCount]++;
	_currentScanline++;

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

		_cpu.RequestInterrupt(InterruptFlags::VBlankInt);
		break;
	}

	if (_registers[RegLcdStatus] & interruptMask)
	{
		_cpu.RequestInterrupt(InterruptFlags::LcdStatInt);
	}
}
