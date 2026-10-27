#pragma once
#include <memory>
#include "Cartridge.h"
#include "GbInternalRom.h"
#include "Graphics.h"
#include "Timer.h"
#include "Apu.h"
#include "InputJoypad.h"
#include "Serial.h"
#include "OamDma.h"
#include "InfraredPort.h"
#include "UndocumentedRegisters.h"


class StateWriter;
class StateReader;

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
	static const unsigned short SerialPorts	  = 0xff01;
	static const unsigned short TimerPorts	  = 0xff04;
	static const unsigned short AfterTimerPorts	= 0xff08;
	static const unsigned short SoundPorts	  = 0xff10;
	static const unsigned short VramRegisters = 0xff40;
	static const unsigned short UnusableArea2 = 0xff4c;
	static const unsigned short HighRam		  = 0xff80;

	static const size_t RomBankSize = 1 << 14;
	static const size_t RamBankSize = 1 << 13;

	// Work RAM: the DMG has 8 KB, the CGB eight 4 KB banks, of which $D000-$DFFF shows the one SVBK selects
	static const size_t WorkRamBankSize = 1 << 12;
	static const int WorkRamBanks = 8;

	// CGB registers the memory map handles itself, or passes to components that have no other registers
	static const unsigned short CpuModeRegister = 0xff4c;
	static const unsigned short InfraredRegister = 0xff56;
	static const unsigned short WorkRamBankRegister = 0xff70;

	static const unsigned short JoypadPort = 0xff00;
	static const unsigned short DividerPort = 0xff04;
	static const unsigned short InternalRomDisable = 0xff50;
	static const unsigned short OamDmaPort = 0xff46;

	static const unsigned short Joypad = 0xff00;

protected:
	std::shared_ptr<Cartridge> _cartridge;
	Graphics* _graphics;
	Timer* _timer;
	Apu* _apu;
	Serial* _serial;
	OamDma* _oamDma;
	InputJoypad& _joypad;

	// Zeroed for repeatability, though their contents at power-on are random on hardware. The DMG uses the first
	// two banks of work RAM
	unsigned char _workRam[WorkRamBanks * WorkRamBankSize]{};
	unsigned char _highRam[128]{};

	// The offset into _workRam of the bank at $D000-$DFFF, and SVBK's bits 0-2, where 0 selects bank 1
	unsigned int _workRamBankOffset{ WorkRamBankSize };
	unsigned char _workRamBankRegister{ 0 };

	bool _cgbMode{ false };
	InfraredPort _infraredPort;
	UndocumentedRegisters _undocumentedRegisters;

	unsigned char ReadWorkRam(unsigned short address) const
	{
		return address & WorkRamBankSize ? _workRam[_workRamBankOffset + (address & (WorkRamBankSize - 1))] : _workRam[address & (WorkRamBankSize - 1)];
	}

	unsigned char& WorkRam(unsigned short address)
	{
		return address & WorkRamBankSize ? _workRam[_workRamBankOffset + (address & (WorkRamBankSize - 1))] : _workRam[address & (WorkRamBankSize - 1)];
	}

	GbInternalRom _internalRom;
	bool _internalRomEnabled = true;

public:
	explicit MemoryMap(InputJoypad& joypad);
	~MemoryMap();

	void SetCartridge(std::shared_ptr<Cartridge> cartridge) { _cartridge = cartridge; }
	void SetGraphics(Graphics* graphics) { _graphics = graphics; }
	void SetTimer(Timer* timer) { _timer = timer; }
	void SetApu(Apu* apu) { _apu = apu; }
	void SetSerial(Serial* serial) { _serial = serial; }
	void SetOamDma(OamDma* oamDma) { _oamDma = oamDma; }

	// CGB mode adds work RAM banks and the CGB's registers
	void SetCgbMode(bool cgbMode);

	// The CGB boot ROM has handed over, so is no longer mapped
	void ApplyCgbPostBootState();

	// The boot ROM overlays the cartridge until it hands over by writing to $FF50
	bool InternalRomEnabled() const { return _internalRomEnabled; }

	// The CPU's view of memory, which OAM DMA and the PPU can block
	unsigned char ReadByte(unsigned short address) const;
	void WriteByte(unsigned short address, unsigned char value);

	// What OAM DMA reads, from below $E000
	unsigned char ReadByteForDma(unsigned short address) const;

	// The CPU touched $FE00-$FEFF; see Graphics::CorruptOam
	void CorruptOam(OamCorruption corruption) { _graphics->CorruptOam(corruption); }

	// Save states: work RAM, high RAM and whether the boot ROM is mapped. The other components keep their own
	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);

private:
	unsigned char ReadUnblocked(unsigned short address) const;

	// $FF4C-$FF7F in CGB mode
	unsigned char ReadCgbRegister(unsigned short address) const;
	void WriteCgbRegister(unsigned short address, unsigned char value);
};

