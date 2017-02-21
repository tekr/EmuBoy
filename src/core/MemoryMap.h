#pragma once
#include <memory>
#include "Cartridge.h"
#include "GbInternalRom.h"
#include "Graphics.h"
#include "Timer.h"
#include "InputJoypad.h"

class MemoryMap
{
public:
	static const unsigned short RomFixed	  = 0;
	static const unsigned short RomSwitched	  = 0x4000;
	static const unsigned short RamVideo	  = 0x8000;
	static const unsigned short RamSwitched	  = 0xa000;
	static const unsigned short RamFixed	  = 0xc000;
	static const unsigned short RamOam		  = 0xfe00;
	static const unsigned short UnusableArea1 = 0xfea0;
	static const unsigned short IoPorts		  = 0xff00;
	static const unsigned short TimerPorts	  = 0xff04;
	static const unsigned short AfterTimerPorts	= 0xff08;
	static const unsigned short VramRegisters = 0xff40;
	static const unsigned short UnusableArea2 = 0xff4c;
	static const unsigned short HighRam		  = 0xff80;

	static const size_t RomBankSize = 1 << 14;
	static const size_t RamBankSize = 1 << 13;

	static const unsigned short JoypadPort = 0xff00;
	static const unsigned short InternalRomDisable = 0xff50;

	static const unsigned short Joypad = 0xff00;

protected:
	std::shared_ptr<Cartridge> _cartridge;
	Graphics* _graphics;
	Timer* _timer;
	InputJoypad& _joypad;

	unsigned char _fixedRam[RamBankSize];
	unsigned char _highRam[128];

	GbInternalRom _internalRom;
	bool _internalRomEnabled = true;

public:
	explicit MemoryMap(InputJoypad& joypad);
	~MemoryMap();

	void SetCartridge(std::shared_ptr<Cartridge> cartridge) { _cartridge = cartridge; }
	void SetGraphics(Graphics* graphics) { _graphics = graphics; }
	void SetTimer(Timer* timer) { _timer = timer; }

	unsigned char ReadByte(unsigned short address) const;
	void WriteByte(unsigned short address, unsigned char value);
};

