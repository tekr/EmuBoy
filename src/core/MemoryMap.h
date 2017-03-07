#pragma once
#include "Cartridge.h"
#include "GbInternalRom.h"
#include "Graphics.h"
#include "Timer.h"
#include "InputJoypad.h"

class MemoryMap
{
public:
	static const uint16_t RomFixed			= 0;
	static const uint16_t RomSwitched		= 0x4000;
	static const uint16_t RamVideo			= 0x8000;
	static const uint16_t RamSwitched		= 0xa000;
	static const uint16_t RamFixed			= 0xc000;
	static const uint16_t RamOam			= 0xfe00;
	static const uint16_t UnusableArea1		= 0xfea0;
	static const uint16_t IoPorts			= 0xff00;
	static const uint16_t TimerPorts		= 0xff04;
	static const uint16_t AfterTimerPorts	= 0xff08;
	static const uint16_t VramRegisters		= 0xff40;
	static const uint16_t UnusableArea2		= 0xff4c;
	static const uint16_t HighRam			= 0xff80;

	static const size_t RomBankSize = 1 << 14;
	static const size_t RamBankSize = 1 << 13;

	static const uint16_t JoypadPort = 0xff00;
	static const uint16_t InternalRomDisable = 0xff50;

protected:
	Cartridge* _cartridge;
	Graphics* _graphics;
	Timer* _timer;
	InputJoypad& _joypad;

	uint8_t _fixedRam[RamBankSize];
	uint8_t _highRam[128];

	GbInternalRom _internalRom;
	bool _internalRomEnabled = true;

public:
	explicit MemoryMap(InputJoypad& joypad);
	~MemoryMap();

	void SetCartridge(Cartridge* cartridge) { _cartridge = cartridge; }
	void SetGraphics(Graphics* graphics) { _graphics = graphics; }
	void SetTimer(Timer* timer) { _timer = timer; }

	uint8_t ReadByte(uint16_t address) const;
	void WriteByte(uint16_t address, uint8_t value);
};

