#include "stdafx.h"
#include "Graphics.h"
#include "MemoryMap.h"
#include "Cpu.h"
#include "OamDma.h"
#include "SpriteManager.h"
#include <algorithm>
#include <climits>
#include <cstring>

// Line timing
// -----------
// Positions are in clocks since LY changed, and everything happens on machine-cycle boundaries, which is when the
// CPU sees it. A line lasts 456 clocks (https://gbdev.io/pandocs/Rendering.html). Mooneye-test-suite's
// lcdon_timing-GS and lcdon_write_timing-GS (https://github.com/Gekkio/mooneye-test-suite) pin down the details,
// which match SameBoy's (https://github.com/LIJI32/SameBoy):
//
//   0       LY changes. STAT still shows mode 0 for a machine cycle, and its LY=LYC flag reads 0 (except on line 0),
//           but the CPU already can't read OAM
//   4       Mode 2: the OAM scan, reading a row of OAM each machine cycle, during which the CPU can't write OAM
//   80      The CPU can write OAM again for the scan's last machine cycle, but can't read VRAM
//   84      Mode 3: the line is drawn and the CPU can't access VRAM or OAM. It lasts 172 clocks plus penalties
//   256+    Mode 0 (H-Blank)
//
// In V-Blank, LY changes at 0 and is compared with LYC from 4, when, on line 144, mode 1 and the V-Blank interrupt
// start. On line 153 LY reads 153 only until 4, then 0; LYC matches 153 from 4 and 0 from 12.
//
// Turning the LCD on starts line 0 at position 4 (so it ends 452 clocks later) with no OAM scan: STAT shows mode 0
// and the CPU can access OAM and VRAM until mode 3, which starts at 84 as on other lines.

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

	return ShadeColour(paletteData >> (colour << 1) & 0x3);
}

Graphics::Graphics(Cpu& cpu, MemoryMap& memoryMap, SpriteManager& spriteManager)
	: _cpu(cpu), _memoryMap(memoryMap), _spriteManager(spriteManager)
{
	_memoryMap.SetGraphics(this);

	_registers[RegBgWinPalette] = 0xff;
	_registers[RegSprite0Palette] = 0xff;
	_registers[RegSprite1Palette] = 0xff;

	memset(Bitmap, 0xff, sizeof(Bitmap));
}

void Graphics::RunLineEvent()
{
	switch (_nextEvent)
	{
	case LineEvent::OamScan:
		StartOamScan();
		break;

	case LineEvent::OamScanLastCycle:
		EndOamScan();
		break;

	case LineEvent::Draw:
		StartDrawing();
		break;

	case LineEvent::HBlank:
		StartHBlank();
		break;

	case LineEvent::VBlankCompare:
		CompareInVBlank();
		break;

	case LineEvent::Line153StopCompare:
		SetLyForComparison(NoComparison, LineEvent::Line153CompareZero, Line153CompareZeroStart);
		break;

	case LineEvent::Line153CompareZero:
		SetLyForComparison(0, LineEvent::LineEnd, ScanlineClocks);
		break;

	case LineEvent::LineEnd:
		_linePosition -= ScanlineClocks;
		StartLine(_currentScanline + 1 < FrameLines ? _currentScanline + 1 : 0);
		break;
	}
}

void Graphics::StartLine(unsigned int line)
{
	_currentScanline = line;
	_registers[RegLineCount] = static_cast<unsigned char>(line);
	_lyForComparison = line == 0 ? 0 : NoComparison;

	if (line < VertPixels)
	{
		if (line == 0) _currentWindowScanline = 0;

		_mode = LcdcStatus::HBlankMode;
		_oamReadBlocked = true;
		_oamScanning = true;
		_dmaDuringOamScan = false;

		UpdateStat();
		Schedule(LineEvent::OamScan, OamScanStart);
	}
	else
	{
		UpdateStat();
		Schedule(LineEvent::VBlankCompare, OamScanStart);
	}
}

void Graphics::StartOamScan()
{
	_mode = LcdcStatus::OamReadMode;
	_oamWriteBlocked = true;
	_lyForComparison = _currentScanline;
	if (_oamDma != nullptr && _oamDma->IsTransferring()) _dmaDuringOamScan = true;

	// The mode 2 condition feeds the STAT interrupt line only as mode 2 starts, not for the rest of it, as in
	// SameBoy. So writing STAT (the STAT write bug) in mode 2 doesn't request an interrupt for mode 2, as
	// gbmicrotest's stat_write_glitch_l1_d shows on a DMG
	_interruptMode = LcdcStatus::OamReadMode;
	UpdateStat();
	_interruptMode = NoInterruptMode;
	UpdateStat();

	Schedule(LineEvent::OamScanLastCycle, OamScanLastCycle);
}

void Graphics::EndOamScan()
{
	_oamWriteBlocked = false;
	_vramReadBlocked = true;
	_oamScanning = false;
	if (_oamDma != nullptr && _oamDma->IsTransferring()) _dmaDuringOamScan = true;

	Schedule(LineEvent::Draw, DrawStart);
}

void Graphics::StartDrawing()
{
	_mode = LcdcStatus::OamAndVramReadMode;
	_interruptMode = NoInterruptMode;

	SetBlocking(true, true);
	UpdateStat();

	Schedule(LineEvent::HBlank, DrawStart + DrawLine());
}

void Graphics::StartHBlank()
{
	_mode = LcdcStatus::HBlankMode;
	_interruptMode = LcdcStatus::HBlankMode;
	SetBlocking(false, false);

	UpdateStat();
	Schedule(LineEvent::LineEnd, ScanlineClocks);
}

void Graphics::CompareInVBlank()
{
	if (_currentScanline == FrameLines - 1)
	{
		// LY reads 0 for all but the first machine cycle of the last line
		_registers[RegLineCount] = 0;
		_lyForComparison = _currentScanline;
		UpdateStat();
		Schedule(LineEvent::Line153StopCompare, Line153StopCompareStart);
		return;
	}

	_lyForComparison = _currentScanline;

	if (_currentScanline == VertPixels)
	{
		_mode = LcdcStatus::VBlankMode;
		_cpu.RequestInterrupt(InterruptFlags::VBlankInt);

		// The mode 2 interrupt select also requests a STAT interrupt as V-Blank starts, at the same time as the
		// V-Blank interrupt (mooneye-test-suite's vblank_stat_intr-GS)
		if ((_registers[RegLcdStatus] & OamIntSelect) && !_statLine) _cpu.RequestInterrupt(InterruptFlags::LcdStatInt);

		_interruptMode = LcdcStatus::VBlankMode;
		CompleteFrame();
	}

	UpdateStat();
	Schedule(LineEvent::LineEnd, ScanlineClocks);
}

void Graphics::SetLyForComparison(int line, LineEvent nextEvent, int nextPosition)
{
	_lyForComparison = line;
	UpdateStat();
	Schedule(nextEvent, nextPosition);
}

void Graphics::TurnOn()
{
	_currentScanline = 0;
	_currentWindowScanline = 0;
	_registers[RegLineCount] = 0;
	_linePosition = FirstLineStart;
	_blankFrame = true;

	_mode = LcdcStatus::HBlankMode;
	_interruptMode = NoInterruptMode;
	_lyForComparison = 0;
	_oamScanning = false;
	_dmaDuringOamScan = false;
	SetBlocking(false, false);

	UpdateStat();
	Schedule(LineEvent::Draw, DrawStart);
}

void Graphics::TurnOff()
{
	// LY reads 0 and STAT mode 0, while the LY=LYC flag and the STAT interrupt line keep their values
	// (mooneye-test-suite's stat_lyc_onoff)
	_registers[RegLineCount] = 0;
	_mode = LcdcStatus::HBlankMode;
	_oamScanning = false;
	SetBlocking(false, false);
}

void Graphics::CompleteFrame()
{
	if (!DisplayEnabled() || _blankFrame) std::fill(std::begin(Bitmap), std::end(Bitmap), BlankColour);
	_blankFrame = false;

	_frameCompleted = true;

	// While the LCD is off frames keep to the cadence; when it's on they come with V-Blank
	_framePosition = DisplayEnabled() ? 0 : _framePosition - FrameClocks;
}

void Graphics::SetBlocking(bool oam, bool vram)
{
	_oamReadBlocked = _oamWriteBlocked = oam;
	_vramReadBlocked = _vramWriteBlocked = vram;
}

bool Graphics::StatLineFor(unsigned char intSelects) const
{
	switch (_interruptMode)
	{
	case LcdcStatus::HBlankMode:
		if (intSelects & HBlankIntSelect) return true;
		break;

	case LcdcStatus::VBlankMode:
		if (intSelects & VBlankIntSelect) return true;
		break;

	case LcdcStatus::OamReadMode:
		if (intSelects & OamIntSelect) return true;
		break;
	}

	return (intSelects & LineCompareIntSelect) && _lineCompareMatch;
}

void Graphics::SetStatLine(bool line)
{
	if (line && !_statLine) _cpu.RequestInterrupt(InterruptFlags::LcdStatInt);
	_statLine = line;
}

void Graphics::UpdateStat()
{
	// Nothing is compared while the LCD is off
	if (!DisplayEnabled()) return;

	if (_lyForComparison != NoComparison)
	{
		_lineCompareMatch = _lyForComparison == _registers[RegLineCompare];
		_lineCompareFlag = _lineCompareMatch;
	}
	else
	{
		_lineCompareFlag = false;
	}

	SetStatLine(StatLineFor(_registers[RegLcdStatus]));
}

void Graphics::WriteOam(unsigned short address, unsigned char value)
{
	if (!_oamWriteBlocked) WriteOamDirect(address, value);
}

void Graphics::WriteOamDirect(unsigned short address, unsigned char value)
{
	auto locationChanged = address % 4 < 2 && value != _oam[address];
	_oam[address] = value;

	if (locationChanged) SpriteChanged(address);
}

void Graphics::SpriteChanged(unsigned short address)
{
	_spriteManager.SpriteMoved(*reinterpret_cast<SpriteData*>(&_oam[address & 0xfffc]));
}

void Graphics::CorruptOam(OamCorruption corruption)
{
	if (!_oamScanning) return;

	// OAM is read as 16-bit words, in 20 rows of four words (two sprites), one row per machine cycle of the scan
	const int rowBytes = 8;
	const int rows = OamSize / rowBytes;
	auto row = _linePosition / 4;

	// The first row (the first two sprites) is never corrupted
	if (row < 1 || row >= rows) return;

	auto word = [this](int row, int index)
	{
		auto address = row * rowBytes + index * 2;
		return static_cast<unsigned short>(_oam[address] | _oam[address + 1] << 8);
	};
	auto setWord = [this](int row, int index, unsigned short value)
	{
		auto address = row * rowBytes + index * 2;
		_oam[address] = value & 0xff;
		_oam[address + 1] = value >> 8;
	};
	auto copyRow = [this](int from, int to) { memcpy(&_oam[to * rowBytes], &_oam[from * rowBytes], rowBytes); };

	auto firstChangedRow = row;

	if (corruption == OamCorruption::ReadDuringIncDec && row >= 4 && row < rows - 1)
	{
		auto a = word(row - 2, 0), b = word(row - 1, 0), c = word(row, 0), d = word(row - 1, 2);
		setWord(row - 1, 0, (b & (a | c | d)) | (a & c & d));
		copyRow(row - 1, row);
		copyRow(row - 1, row - 2);
		firstChangedRow = row - 2;
	}

	auto a = word(row, 0), b = word(row - 1, 0), c = word(row - 1, 2);
	setWord(row, 0, corruption == OamCorruption::Write ? ((a ^ c) & (b ^ c)) ^ c : b | (a & c));
	for (auto index = 1; index < 4; index++) setWord(row, index, word(row - 1, index));

	for (auto changed = firstChangedRow; changed <= row; changed++)
	{
		SpriteChanged(static_cast<unsigned short>(changed * rowBytes));
		SpriteChanged(static_cast<unsigned short>(changed * rowBytes + 4));
	}
}

unsigned char Graphics::ReadRegister(unsigned short address) const
{
	if (address == RegLcdStatus)
	{
		// Bit 7 always reads 1
		return 0x80 | _registers[RegLcdStatus] | (_lineCompareFlag ? LineCompareFlag : 0) | _mode;
	}

	return _registers[address];
}

void Graphics::WriteRegister(unsigned short address, unsigned char value)
{
	switch (address)
	{
	case RegLineCount:
		// LY is read-only (Pan Docs)
		return;

	case RegLcdControl:
	{
		_spriteManager.SetUseTallSprites(value & 0x4);

		auto wasEnabled = DisplayEnabled();
		_registers[RegLcdControl] = value;

		if (wasEnabled && !DisplayEnabled()) TurnOff();
		else if (!wasEnabled && DisplayEnabled()) TurnOn();
		return;
	}

	case RegLcdStatus:
		// The STAT write bug: per https://gbdev.io/pandocs/STAT.html#spurious-stat-interrupts, the DMG behaves as if
		// $FF were written first, so a write can request an interrupt in modes 0 and 1 or while LY=LYC (in mode 2
		// too, says Pan Docs, but see StartOamScan). Pan Docs says $FF lasts a machine cycle, but gbmicrotest's
		// stat_write_glitch_l1_a shows it doesn't on a DMG: a write in mode 3's last machine cycle doesn't select
		// mode 0 as it starts. In SameBoy it lasts a single clock, so both values apply in the write's machine cycle
		if (DisplayEnabled()) SetStatLine(StatLineFor(AllIntSelects));

		_registers[RegLcdStatus] = value & AllIntSelects;
		UpdateStat();
		return;

	case RegLineCompare:
		_registers[RegLineCompare] = value;
		UpdateStat();
		return;

	default:
		_registers[address] = value;
	}
}

int Graphics::DrawLine()
{
	// A rather inefficient rendering implementation.
	// Many intermediate results could be lifted outside loops

	auto backgroundY = _currentScanline + _registers[RegBgScrollY];
	auto windowVisibleThisLine = WindowEnabled() && _currentScanline >= _registers[RegWindowY] && _registers[RegWindowX] < 167;
	auto windowStartX = _registers[RegWindowX] - 7;

	if (_dmaDuringOamScan) _spriteManager.ClearVisibleSprites();
	else _spriteManager.SelectSprites(_currentScanline);

	auto& visibleSprites = _spriteManager.GetVisibleSprites();
	auto firstSpriteNotLeftOfX = visibleSprites.cbegin();
	auto firstSpriteBelowY = visibleSprites.cend();

	auto backgroundEnabled = BackgroundEnabled();
	auto spritesEnabled = SpritesEnabled();

	for (auto x = 0; x < static_cast<int>(HozPixels); x++)
	{
		unsigned char colour = 0;
		auto windowX = x - windowStartX;

		// On the DMG, LCDC bit 0 blanks the window as well as the background
		if (backgroundEnabled)
		{
			if (windowVisibleThisLine && windowX >= 0)
			{
				// Window is over background
				colour = GetBgOrWinColour(windowX, _currentWindowScanline, TileType::Window);
			}
			else
			{
				auto backgroundX = x + _registers[RegBgScrollX];
				colour = GetBgOrWinColour(backgroundX, backgroundY, TileType::Background);
			}
		}

		auto spritePixelOnTop = false;
		auto& pixel = Bitmap[_currentScanline*HozPixels + x];

		if (spritesEnabled)
		{
			while (firstSpriteNotLeftOfX != firstSpriteBelowY && (*firstSpriteNotLeftOfX)->XPos - SpriteManager::SpriteXOffset + SpriteManager::SpriteWidth <= x)
			{
				++firstSpriteNotLeftOfX;
			}

			auto sprite = firstSpriteNotLeftOfX;

			// The highest-priority sprite with a non-transparent pixel here wins; only then does
			// its priority flag decide whether it is drawn over or behind the BG/Window
			while (sprite != firstSpriteBelowY && (*sprite)->XPos - SpriteManager::SpriteXOffset <= x)
			{
				auto spriteColour = _spriteManager.GetSpriteColour(**sprite, x, _currentScanline, _vram);

				if (spriteColour != 0)
				{
					spritePixelOnTop = !((*sprite)->Flags & SpriteFlags::ZPriority) || colour == 0;
					if (spritePixelOnTop) pixel = MapColour(spriteColour, (*sprite)->Flags & SpriteFlags::PaletteSelector ? Palette::Sprite1 : Palette::Sprite0);
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

	// The window's line counter advances even while LCDC bit 0 blanks it, as in SameBoy, where that bit only
	// affects the pixels output, not the window's fetching
	if (windowVisibleThisLine)
	{
		++_currentWindowScanline;
	}

	// Per https://gbdev.io/pandocs/Rendering.html#mode-3-length, mode 3 is paused while SCX % 8 pixels are
	// discarded, and by the window and sprites. Mode 0 starts on the machine cycle after mode 3 ends, so the SCX
	// pause rounds up to whole machine cycles, as mooneye-test-suite's hblank_ly_scx_timing-GS shows: 1-4 pixels
	// add one, 5-7 add two. The other penalties round down (see DrawPenalty)
	return MinDrawClocks + _registers[RegBgScrollX] % 8 + DrawPenalty(windowVisibleThisLine, windowStartX);
}

int Graphics::DrawPenalty(bool windowVisible, int windowStartX) const
{
	const int windowPenalty = 6;
	const int spriteFetchPenalty = 6;
	const int offScreenLeftPenalty = 11;
	const int offScreenLeftTile = INT_MIN;
	const int windowTilesOffset = 0x1000;

	auto penalty = windowVisible ? windowPenalty : 0;

	if (SpritesEnabled())
	{
		// Per https://gbdev.io/pandocs/Rendering.html#obj-penalty-algorithm, sprites are taken from left to right.
		// Each waits for the fetch of the background or window tile its leftmost pixel is in to finish, which
		// only the first sprite in each tile has to do, then takes 6 clocks to fetch
		auto lastTileConsidered = INT_MAX;

		for (auto sprite : _spriteManager.GetVisibleSprites())
		{
			if (sprite->XPos >= HozPixels + SpriteManager::SpriteXOffset) continue;

			int tile, tileWait;

			if (sprite->XPos == 0)
			{
				tile = offScreenLeftTile;
				tileWait = offScreenLeftPenalty - spriteFetchPenalty;
			}
			else
			{
				auto x = sprite->XPos - SpriteManager::SpriteXOffset;
				auto inWindow = windowVisible && x >= windowStartX;

				// Keeps the window's tiles apart from the background's, and the left of the screen non-negative
				auto tileX = inWindow ? x - windowStartX + windowTilesOffset : x + _registers[RegBgScrollX] + 8;
				tile = tileX >> 3;
				tileWait = std::max(7 - (tileX & 7) - 2, 0);
			}

			if (tile != lastTileConsidered) penalty += tileWait;
			lastTileConsidered = tile;
			penalty += spriteFetchPenalty;
		}
	}

	// Mooneye-test-suite's intr_2_mode0_timing_sprites shows that these penalties lengthen mode 3 by whole machine
	// cycles, rounding down, unlike SCX's pause
	return penalty & ~3;
}
