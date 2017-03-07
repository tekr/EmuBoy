#pragma once
#include <cstdint>

class MemoryMap;
class Cpu;
class SpriteManager;

enum LcdcStatus : uint8_t
{
	HBlankMode			= 0,
	VBlankMode			= 1,
	OamReadMode			= 2,
	OamAndVramReadMode	= 3
};

enum SpriteFlags : uint8_t
{
	PaletteSelector = 0x10,
	XFlip			= 0x20,
	YFlip			= 0x40,
	ZPriority		= 0x80
};

// Windows-specific
#pragma pack(push, 1)
struct SpriteData
{
	uint8_t YPos;
	uint8_t XPos;
	uint8_t PatternNum;
	SpriteFlags Flags;

	uintptr_t SpriteData::OrderedSpriteId() const
	{
		return reinterpret_cast<uintptr_t>(this);
	}
};
// Windows-specific
#pragma pack(pop)

class Graphics
{
public:
	static const unsigned int SystemClockHz = 4194304;

	static const unsigned int HozPixels = 160;
	static const unsigned int VertPixels = 144;

	static const unsigned int HBlankPeriodClocks = 204;		// Docs say 201-207
	static const unsigned int OamReadClocks = 80;			// Docs say 77-83
	static const unsigned int OamAndVramReadClocks = 172;	// Docs say 169-175

	static const unsigned int ScanlineClocks = HBlankPeriodClocks + OamReadClocks + OamAndVramReadClocks;
	static_assert(ScanlineClocks == 456, "Clocks per scanline is incorrect");

	static const unsigned int VBlankLines = 10;
	static const unsigned int FrameClocks = ScanlineClocks * (VertPixels + VBlankLines);
	static_assert(FrameClocks == 70224, "Clocks per frame is incorrect");

	const double FrameRate = static_cast<double>(SystemClockHz) / FrameClocks;
	static const unsigned int VramSize = 1 << 13;
	static const unsigned int OamSize = 160;

	static const uint16_t SpriteDataTableBase = 0;

protected:
	static const unsigned int RegisterBlockSize = 0xb;

	static const uint16_t TileDataTableBase1 = 0;
	static const uint16_t TileDataTableBase2 = 0x800;

	static const uint16_t TileMapBase1 = 0x1800;
	static const uint16_t TileMapBase2 = 0x1c00;

	static const unsigned int RegLcdControl		= 0x0;
	static const unsigned int RegLcdStatus		= 0x1;
	static const unsigned int RegBgScrollY		= 0x2;
	static const unsigned int RegBgScrollX		= 0x3;
	static const unsigned int RegLineCount		= 0x4;
	static const unsigned int RegLineCompare	= 0x5;
	static const unsigned int RegDmaTransfer	= 0x6;
	static const unsigned int RegBgWinPalette	= 0x7;
	static const unsigned int RegSprite0Palette = 0x8;
	static const unsigned int RegSprite1Palette = 0x9;
	static const unsigned int RegWindowY		= 0xa;
	static const unsigned int RegWindowX		= 0xb;

	Cpu& _cpu;
	MemoryMap& _memoryMap;

	bool _screenEnabled;
	LcdcStatus _status{ LcdcStatus::VBlankMode };

	uint64_t _totalCycles;

	uint8_t _vram[VramSize];
	uint8_t _oam[OamSize];

	uint8_t _registers[RegisterBlockSize];

	unsigned int _currentScanline;
	unsigned int _currentWindowScanline;

	bool DisplayEnabled() const { return (_registers[RegLcdControl] & 0x80) != 0; }
	bool WindowEnabled() const { return (_registers[RegLcdControl] & 0x20) != 0; }
	bool BackgroundEnabled() const { return (_registers[RegLcdControl] & 0x1) != 0; }
	bool SpritesEnabled() const { return (_registers[RegLcdControl] & 0x2) != 0; }

	static int GetTileOffset(int x, int y) { return ((y & 0xf8) << 2) + ((x & 0xff) >> 3); }

	enum class TileType { Background, Window };
	enum class Palette { BgAndWindow, Sprite0, Sprite1 };

	uint8_t GetBgOrWinColour(int x, int y, TileType tileType) const;

	int MapColour(uint8_t colour, Palette palette);

	// Used as a dummy read/write location when an attempt is made to access
	// VRAM or OAM during periods when it is inaccessible on the real hardware
	uint8_t _dummy;

	SpriteManager& _spriteManager;

	void CheckLineCompare();

public:

	int Bitmap[HozPixels * VertPixels];

	Graphics(Cpu& cpu, MemoryMap& memoryMap, SpriteManager& spriteManager);

	uint8_t& Vram(uint16_t address) { return _status != LcdcStatus::OamAndVramReadMode ? _vram[address] : _dummy; }

	uint8_t ReadOam(uint16_t address) { return _status != LcdcStatus::OamReadMode && _status != LcdcStatus::OamAndVramReadMode
															? _oam[address] : 0xff; }

	void WriteOam(uint16_t address, uint8_t value);

	uint8_t ReadRegister(uint16_t address) const { return _registers[address] | (address == RegLcdStatus ? 0x80 : 0); }

	void WriteRegister(uint16_t address, uint8_t value);

	void ResetFrame()
	{
		_currentScanline = 0;
		_currentWindowScanline = 0;
	}

	int RenderLine();

	void SetLcdcStatus(LcdcStatus status);
};