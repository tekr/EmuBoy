#include "stdafx.h"
#include "MemoryMap.h"

MemoryMap::MemoryMap(InputJoypad& joypad) : _graphics(nullptr), _timer(nullptr), _apu(nullptr), _serial(nullptr), _oamDma(nullptr), _joypad(joypad)
{
}

MemoryMap::~MemoryMap()
{
}

unsigned char MemoryMap::ReadByte(unsigned short address) const
{
	if (_oamDma != nullptr && _oamDma->BlocksCpuAccess(address)) return _oamDma->BlockedRead(address);

	return ReadUnblocked(address);
}

unsigned char MemoryMap::ReadByteForDma(unsigned short address) const
{
	if (address >= RamVideo && address < RamSwitched) return _graphics->Vram(address - RamVideo);

	return ReadUnblocked(address);
}

unsigned char MemoryMap::ReadUnblocked(unsigned short address) const
{
	if (address < RamVideo)
	{
		return _internalRomEnabled && address < 0x100
			? _internalRom.ReadByte(address)
			: _cartridge->RomReadByte(address);
	}

	if (address < RamSwitched)
	{
		return _graphics->ReadVram(address - RamVideo);
	}

	if (address < RamFixed)
	{
		return _cartridge->RamReadByte(address - RamSwitched);
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
		// The unusable area from 0xfea0 to 0xfeff reads 0 on the DMG, or $FF while OAM is blocked
		return _graphics->OamReadBlocked() ? 0xff : 0;
	}

	if (address < TimerPorts)
	{
		if (address == JoypadPort) return _joypad.ReadRegister();
		if (address < SerialPorts + 2 && _serial != nullptr) return _serial->ReadRegister(address - SerialPorts);

		// TODO: IO ports
		return 0xff;
	}

	if (address < AfterTimerPorts)
	{
		return _timer->ReadRegister(address - TimerPorts);
	}

	if (address < SoundPorts)
	{
		// TODO: IO ports
		return 0xff;
	}

	if (address < VramRegisters)
	{
		return _apu != nullptr ? _apu->ReadRegister(address) : 0xff;
	}

	if (address < UnusableArea2)
	{
		if (address == OamDmaPort && _oamDma != nullptr) return _oamDma->ReadRegister();
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
	if (_oamDma != nullptr && _oamDma->BlocksCpuAccess(address)) return;

	if (address < RamVideo)
	{
		// Writing to ROM area. Used to switch the cartridge's memory banks
		_cartridge->RomWriteByte(address, value);
	}
	else if (address < RamSwitched)
	{
		_graphics->WriteVram(address - RamVideo, value);
	}
	else if (address < RamFixed)
	{
		_cartridge->RamWriteByte(address - RamSwitched, value);
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
		else if (address < SerialPorts + 2 && _serial != nullptr) _serial->WriteRegister(address - SerialPorts, value);

		// TODO: I/O ports
	}
	else if (address < AfterTimerPorts)
	{
		_timer->WriteRegister(address - TimerPorts, value);

		if (address == TimerPorts && _apu != nullptr) _apu->ResetDivider();
	}
	else if (address < SoundPorts)
	{
		// TODO: I/O ports
	}
	else if (address < VramRegisters)
	{
		if (_apu != nullptr) _apu->WriteRegister(address, value);
	}
	else if (address < UnusableArea2)
	{
		if (address == OamDmaPort && _oamDma != nullptr) _oamDma->WriteRegister(value);
		else _graphics->WriteRegister(address - VramRegisters, value);
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
