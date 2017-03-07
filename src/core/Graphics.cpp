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

	// 75% opacity to emulate the slow GB screen response time. Green tinge to look more like the original
	return 0xc0000000 | ((3 - (paletteData >> (colour << 1) & 0x3)) * 0x40504a);
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
	else if (address == RegLcdControl)
	{
		_spriteManager.SetUseTallSprites(value & 0x4);

		// Per GB programming manual p.56, turning off the display immediately resets the line count
		if (!(value & 0x80) && _registers[RegLcdControl] & 0x80) _registers[RegLineCount] = 0;
	}
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
	else if (address == RegLcdStatus)
	{
		// Prevent changing read-only bits, clear match flag in bit 2 on write
		value = value & 0xf8 | _registers[RegLcdStatus] & 0x3;

		// Hardware quirk that at least Roadrash & Legend of Zerd rely on
		// Per http://www.devrs.com/gb/files/faqs.html#GBBugs
		if (DisplayEnabled() && _registers[RegLcdStatus] & (LcdcStatus::HBlankMode | LcdcStatus::VBlankMode))
		{
			_cpu.RequestInterrupt(InterruptFlags::VBlankInt);
		}
		// Turning display off immediately causes HBlankMode
		else if (!(value & 0x80)) value = value & 0xfc | LcdcStatus::HBlankMode;
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
		_currentWindowScanline = 0;
	}

	if (_currentScanline < VertPixels)
	{
		if (DisplayEnabled())
		{
			// A rather inefficient rendering implementation.
			// Many intermediate results could be lifted outside loops

			auto backgroundY = _currentScanline + _registers[RegBgScrollY];
			auto windowVisibleThisLine = WindowEnabled() && _currentScanline >= _registers[RegWindowY] && _registers[RegWindowX] < 167;

			auto visibleSprites = _spriteManager.GetVisibleSprites();
			auto firstSpriteNotLeftOfX = visibleSprites.cbegin();
			auto firstSpriteBelowY = visibleSprites.cend();

			auto spritesThisLine = 0;

			auto backgroundEnabled = BackgroundEnabled();
			auto spritesEnabled = SpritesEnabled();

			for (auto x = 0; x < HozPixels;x++)
			{
				unsigned char colour = 0;
				auto windowX = x - _registers[RegWindowX] + 7;

				if (windowVisibleThisLine && windowX >= 0)
				{
					// Window is over background
					colour = GetBgOrWinColour(windowX, _currentWindowScanline, TileType::Window);
				}
				else if (backgroundEnabled)
				{
					auto backgroundX = x + _registers[RegBgScrollX];
					colour = GetBgOrWinColour(backgroundX, backgroundY, TileType::Background);
				}

				auto spritePixelOnTop = false;
				auto& pixel = Bitmap[_currentScanline*HozPixels + x];

				if (spritesEnabled)
				{
					while (firstSpriteNotLeftOfX != firstSpriteBelowY && (*firstSpriteNotLeftOfX)->XPos - SpriteManager::SpriteXOffset + SpriteManager::SpriteWidth <= x)
					{
						++firstSpriteNotLeftOfX;
						++spritesThisLine;
					}

					auto sprite = firstSpriteNotLeftOfX;

					// Iterate through sprites overlapping this pixel as necessary until we either:
					//  * Hit a non-transparent sprite pixel above the BG/Window
					//  * Hit a sprite that is behind the BG/Window, at which point we use BG/Window pixel (regardless of lower-priority sprite settings)
					//  * Run out of overlapping sprites
					while (sprite != firstSpriteBelowY && (*sprite)->XPos - SpriteManager::SpriteXOffset <= x && spritesThisLine <= SpriteManager::MaxSpritesPerLine &&
						(!((*sprite)->Flags & SpriteFlags::ZPriority) || colour == 0))
					{
						auto spriteColour = _spriteManager.GetSpriteColour(**sprite, x, _currentScanline, _vram);
						spritePixelOnTop = spriteColour != 0;

						if (spritePixelOnTop)
						{
							pixel = MapColour(spriteColour, (*firstSpriteNotLeftOfX)->Flags & SpriteFlags::PaletteSelector ? Palette::Sprite1 : Palette::Sprite0);
							break;
						}

						++sprite;
					}
				}

				if (!spritePixelOnTop)
				{
					pixel = MapColour(colour, Palette::BgAndWindow);
				}
			}

			if (windowVisibleThisLine)
			{
				++_currentWindowScanline;
			}
		}
		else
		{
			memset(&Bitmap[_currentScanline*HozPixels], 0xff, HozPixels*4);
		}
	}

	CheckLineCompare();

	++_registers[RegLineCount];
	++_currentScanline;
	_spriteManager.NextScanline();

	return ScanlineClocks;
}

void Graphics::SetLcdcStatus(LcdcStatus status)
{
	_status = DisplayEnabled() ? status : LcdcStatus::HBlankMode;
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
		// According to http://gameboy.mongenel.com/dmg/istat98.txt, LCD stat interrupt
		// is generated for this mode if either VBlank or OAM stat bits are set
		interruptMask = 0x30;

		_cpu.RequestInterrupt(InterruptFlags::VBlankInt);
		break;
	}

	// TODO: Interrupt is only actually fired if the signal (derived from ORing together
	// the results of ANDing the mode with its corresponding mask register bit) changes
	// from 0 to 1. Explained in detail in 8.7 of
	// https://github.com/AntonioND/giibiiadvance/blob/master/docs/TCAGBD.pdf
	if (_registers[RegLcdStatus] & interruptMask)
	{
		_cpu.RequestInterrupt(InterruptFlags::LcdStatInt);
	}
}
