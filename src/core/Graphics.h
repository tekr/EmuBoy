#pragma once
#include <cstdint>

class MemoryMap;
class Cpu;
class SpriteManager;
class OamDma;

enum LcdcStatus : unsigned char
{
	HBlankMode			= 0,
	VBlankMode			= 1,
	OamReadMode			= 2,
	OamAndVramReadMode	= 3
};

enum SpriteFlags : unsigned char
{
	PaletteSelector = 0x10,
	XFlip			= 0x20,
	YFlip			= 0x40,
	ZPriority		= 0x80
};

// How the CPU touched OAM while the PPU was scanning it, which decides how the DMG's OAM corruption bug corrupts it
// (https://gbdev.io/pandocs/OAM_Corruption_Bug.html)
enum class OamCorruption
{
	// A write, or a 16-bit increment or decrement of a register holding an OAM address
	Write,
	Read,
	// A read in the same machine cycle as an increment or decrement, as by LD A,(HL+)
	ReadDuringIncDec
};

// Windows-specific
#pragma pack(push, 1)
struct SpriteData
{
	unsigned char YPos;
	unsigned char XPos;
	unsigned char PatternNum;
	SpriteFlags Flags;

	uintptr_t OrderedSpriteId() const
	{
		return reinterpret_cast<uintptr_t>(this);
	}
};
// Windows-specific
#pragma pack(pop)

// The PPU. It runs alongside the CPU one machine cycle at a time, drawing each line in one go as the line's mode 3
// starts, and keeping the LCD's timing, STAT interrupts and the CPU's access to VRAM and OAM accurate to the cycle
class Graphics
{
public:
	static const unsigned int SystemClockHz = 4194304;

	static const unsigned int HozPixels = 160;
	static const unsigned int VertPixels = 144;

	static const unsigned int OamScanClocks = 80;
	static const unsigned int MinDrawClocks = 172;
	static const unsigned int ScanlineClocks = 456;

	static const unsigned int VBlankLines = 10;
	static const unsigned int FrameLines = VertPixels + VBlankLines;
	static const unsigned int FrameClocks = ScanlineClocks * FrameLines;
	static_assert(FrameClocks == 70224, "Clocks per frame is incorrect");

	const double FrameRate = static_cast<double>(SystemClockHz) / FrameClocks;
	static const unsigned int VramSize = 1 << 13;
	static const unsigned int OamSize = 160;

	static const unsigned short SpriteDataTableBase = 0;

protected:
	static const unsigned short TileDataTableBase1 = 0;
	static const unsigned short TileDataTableBase2 = 0x800;

	static const unsigned short TileMapBase1 = 0x1800;
	static const unsigned short TileMapBase2 = 0x1c00;

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

	static const unsigned int RegisterBlockSize = RegWindowX + 1;

	// STAT's interrupt selects
	static const unsigned char HBlankIntSelect = 0x08;
	static const unsigned char VBlankIntSelect = 0x10;
	static const unsigned char OamIntSelect = 0x20;
	static const unsigned char LineCompareIntSelect = 0x40;
	static const unsigned char AllIntSelects = 0x78;
	static const unsigned char LineCompareFlag = 0x04;

	// Positions in a line, in clocks since LY changed. See the timing notes in Graphics.cpp
	static const int OamScanStart = 4;
	static const int OamScanLastCycle = OamScanStart + OamScanClocks - 4;
	static const int DrawStart = OamScanStart + OamScanClocks;

	// Line 153 stops comparing LY with LYC, then compares 0
	static const int Line153StopCompareStart = 8;
	static const int Line153CompareZeroStart = 12;

	// Turning the LCD on starts line 0 part-way in, without an OAM scan
	static const int FirstLineStart = 4;

	static const int NoComparison = -1;
	static const int NoInterruptMode = -1;

	enum class LineEvent
	{
		OamScan,
		OamScanLastCycle,
		Draw,
		HBlank,
		VBlankCompare,
		Line153StopCompare,
		Line153CompareZero,
		LineEnd
	};

	Cpu& _cpu;
	MemoryMap& _memoryMap;
	SpriteManager& _spriteManager;
	const OamDma* _oamDma{ nullptr };

	// Zeroed for repeatability, though their contents at power-on are random on hardware
	unsigned char _vram[VramSize]{};
	unsigned char _oam[OamSize]{};

	// All zero at power-on (display off, as the boot ROM expects), apart from the palettes, set in the constructor.
	// STAT's slot holds only its interrupt selects; its mode and LY=LYC flag are kept separately
	unsigned char _registers[RegisterBlockSize]{};

	unsigned int _currentScanline{ 0 };
	unsigned int _currentWindowScanline{ 0 };

	// Clocks since the line started, and the next event in it
	int _linePosition{ 0 };
	int _nextEventPosition{ 0 };
	LineEvent _nextEvent{ LineEvent::LineEnd };

	// The mode STAT shows, and the mode whose interrupt select feeds the STAT interrupt line (or NoInterruptMode).
	// They differ around mode changes, as on hardware, and for all of mode 2, whose condition feeds the line only as
	// it starts
	LcdcStatus _mode{ LcdcStatus::HBlankMode };
	int _interruptMode{ NoInterruptMode };

	// The line LYC is compared with, or NoComparison for the machine cycle after LY changes, when the comparison
	// reads false but doesn't clear the LY=LYC interrupt condition
	int _lyForComparison{ 0 };
	bool _lineCompareFlag{ false };
	bool _lineCompareMatch{ false };

	// The STAT interrupt is requested only when this line, the OR of all the selected conditions, rises, so one
	// condition holding it high blocks interrupts from the others (https://gbdev.io/pandocs/Interrupt_Sources.html)
	bool _statLine{ false };

	bool _oamReadBlocked{ false };
	bool _oamWriteBlocked{ false };
	bool _vramReadBlocked{ false };
	bool _vramWriteBlocked{ false };

	// The PPU reads a row of OAM each machine cycle of mode 2, which the OAM corruption bug corrupts
	bool _oamScanning{ false };

	// OAM DMA during the OAM scan hides every sprite on the line
	bool _dmaDuringOamScan{ false };

	// Clocks since the last frame completed, so frames keep coming every FrameClocks while the LCD is off
	unsigned int _framePosition{ 0 };
	bool _frameCompleted{ false };

	// Per https://gbdev.io/pandocs/LCDC.html#lcdc7--lcd-enable, the screen stays blank for the first frame after
	// the LCD is turned on
	bool _blankFrame{ false };

	bool DisplayEnabled() const { return (_registers[RegLcdControl] & 0x80) != 0; }
	bool WindowEnabled() const { return (_registers[RegLcdControl] & 0x20) != 0; }
	bool BackgroundEnabled() const { return (_registers[RegLcdControl] & 0x1) != 0; }
	bool SpritesEnabled() const { return (_registers[RegLcdControl] & 0x2) != 0; }

	static int GetTileOffset(int x, int y) { return ((y & 0xf8) << 2) + ((x & 0xff) >> 3); }

	enum class TileType { Background, Window };
	enum class Palette { BgAndWindow, Sprite0, Sprite1 };

	unsigned char GetBgOrWinColour(int x, int y, TileType tileType) const;

	int MapColour(unsigned char colour, Palette palette);

	void Schedule(LineEvent event, int position)
	{
		_nextEvent = event;
		_nextEventPosition = position;
	}

	void RunLineEvent();
	void StartLine(unsigned int line);
	void StartOamScan();
	void EndOamScan();
	void StartDrawing();
	void StartHBlank();
	void CompareInVBlank();
	void SetLyForComparison(int line, LineEvent nextEvent, int nextPosition);
	void TurnOn();
	void TurnOff();
	void CompleteFrame();

	void SetBlocking(bool oam, bool vram);

	bool StatLineFor(unsigned char intSelects) const;
	void SetStatLine(bool line);

	// Re-evaluates LY=LYC and the STAT interrupt line after anything they depend on changes
	void UpdateStat();

	// Draws the current line in one go, using the registers, OAM and VRAM as they are as its mode 3 starts, and
	// returns how long mode 3 lasts
	int DrawLine();

	// The clocks the window and the line's sprites add to mode 3
	int DrawPenalty(bool windowVisible, int windowStartX) const;

	void SpriteChanged(unsigned short address);

public:

	int Bitmap[HozPixels * VertPixels];

	Graphics(Cpu& cpu, MemoryMap& memoryMap, SpriteManager& spriteManager);

	void SetOamDma(const OamDma* oamDma) { _oamDma = oamDma; }

	// The Bitmap colour for a DMG shade, 0 (lightest) to 3 (darkest)
	static int ShadeColour(unsigned char shade)
	{
		// 60% opacity to emulate the slow GB screen response time. Green tinge to look more like the original
		return 0x99000000 | ((3 - shade) * 0x40504a);
	}

	// The colour of the screen while the LCD is off
	static const int BlankColour = -1;

	void RunCycles(int cycles)
	{
		_framePosition += cycles;

		if (!DisplayEnabled())
		{
			while (_framePosition >= FrameClocks) CompleteFrame();
			return;
		}

		_linePosition += cycles;
		while (_linePosition >= _nextEventPosition) RunLineEvent();
	}

	// True once per frame: as V-Blank starts, or every FrameClocks while the LCD is off
	bool TakeCompletedFrame()
	{
		auto completed = _frameCompleted;
		_frameCompleted = false;
		return completed;
	}

	// The CPU can't read VRAM while the PPU draws (mode 3), nor OAM while it scans or draws (modes 2 and 3);
	// reads return $FF and writes are ignored
	unsigned char ReadVram(unsigned short address) const { return _vramReadBlocked ? 0xff : _vram[address]; }
	void WriteVram(unsigned short address, unsigned char value) { if (!_vramWriteBlocked) _vram[address] = value; }
	unsigned char ReadOam(unsigned short address) const { return _oamReadBlocked ? 0xff : _oam[address]; }
	void WriteOam(unsigned short address, unsigned char value);

	// Whether the CPU is blocked from reading OAM, including the unusable area after it
	bool OamReadBlocked() const { return _oamReadBlocked; }

	// VRAM and OAM regardless of the PPU, for OAM DMA and tests
	unsigned char& Vram(unsigned short address) { return _vram[address]; }
	void WriteOamDirect(unsigned short address, unsigned char value);

	// Applies the DMG's OAM corruption bug for an access to $FE00-$FEFF, which corrupts OAM only while the PPU is
	// scanning it
	void CorruptOam(OamCorruption corruption);

	unsigned char ReadRegister(unsigned short address) const;
	void WriteRegister(unsigned short address, unsigned char value);
};
