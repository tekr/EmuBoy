#include "stdafx.h"
#include "MemoryMap.h"

MemoryMap::MemoryMap(InputJoypad& joypad) : _joypad(joypad)
{
}

MemoryMap::~MemoryMap()
{
}

unsigned char MemoryMap::ReadByte(unsigned short address) const
{
	if (address < RamVideo)
	{
		return _internalRomEnabled && address < 0x100
			? _internalRom.ReadByte(address)
			: _cartridge->RomReadByte(address);
	}

	if (address < RamSwitched)
	{
		return _graphics->Vram(address - RamVideo);
	}

	if (address < RamFixed)
	{
		return _cartridge->RamReadByte(address - RamFixed);
	}

	// Includes near-complete repeat of fixed RAM from 0xe000 to OAM RAM start
	if (address < RamOam)
	{
		return _fixedRam[address & 0x1fff];
	}

	if (address < UnusableArea1)
	{
		return _graphics->ReadOam(address - RamOam);
	}

	if (address < IoPorts)
	{
		// Unused area from 0xfea0 to 0xfeff
		return 0;
	}

	if (address < TimerPorts)
	{
		if (address == JoypadPort) return _joypad.ReadRegister();

		// TODO: IO ports
		return 0;
	}

	if (address < AfterTimerPorts)
	{
		return _timer->ReadRegister(address - TimerPorts);
	}

	if (address < VramRegisters)
	{
		// TODO: IO ports
		return 0;
	}

	if (address < UnusableArea2)
	{
		return _graphics->ReadRegister(address - VramRegisters);
	}

	if (address < HighRam)
	{
		return 0;
	}

	return _highRam[address - HighRam];
}

void MemoryMap::WriteByte(unsigned short address, unsigned char value)
{
	if (address < RamVideo)
	{
		// Writing to ROM area. Used to switch the cartridge's memory banks
		_cartridge->RomWriteByte(address, value);
	}
	else if (address < RamSwitched)
	{
		_graphics->Vram(address - RamVideo) = value;
	}
	else if (address < RamFixed)
	{
		_cartridge->RamWriteByte(address - RamFixed, value);
	}
	// Includes near-complete repeat of fixed RAM from 0xe000 to OAM RAM start
	else if (address < RamOam)
	{
		_fixedRam[address & 0x1fff] = value;
	}
	else if (address < UnusableArea1)
	{
		_graphics->WriteOam(address - RamOam, value);
	}
	else if (address < IoPorts)
	{
		// Writing to undocumented address space
	}
	else if (address < TimerPorts)
	{
		if (address == JoypadPort) _joypad.WriteRegister(value);

		// TODO: I/O ports
	}
	else if (address < AfterTimerPorts)
	{
		_timer->WriteRegister(address - TimerPorts, value);
	}
	else if (address < VramRegisters)
	{
		// TODO: I/O ports
	}
	else if (address < UnusableArea2)
	{
		_graphics->WriteRegister(address - VramRegisters, value);
	}
	else if (address < HighRam)
	{
		// Writing to undocumented address space (excluding internal ROM disable)
		if (address == 0xff50) _internalRomEnabled &= !value;
	}
	else
	{
		_highRam[address - HighRam] = value;
	}
}
