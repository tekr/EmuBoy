#include "stdafx.h"
#include "MemoryMap.h"
#include "StateStream.h"
#include <algorithm>
#include <iterator>

const unsigned char UndocumentedRegisters::WritableBits[Count] = { 0xff, 0xff, 0xff, 0x70 };

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
		return ReadWorkRam(address);
	}

	if (address < UnusableArea1)
	{
		return _graphics->ReadOam(address - RamOam);
	}

	if (address < IoPorts)
	{
		// The unusable area from 0xfea0 to 0xfeff reads $FF while OAM is blocked, and otherwise 0 on the DMG. On CGB
		// revision E it reads the high nibble of the address's low byte twice (https://gbdev.io/pandocs/Memory_Map.html#fea0feff-range)
		if (_graphics->OamReadBlocked()) return 0xff;
		return _cgbMode ? static_cast<unsigned char>((address & 0xf0) | (address & 0xf0) >> 4) : 0;
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
		// Unmapped I/O ports read $FF on the DMG
		return _cgbMode ? ReadCgbRegister(address) : 0xff;
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
		WorkRam(address) = value;
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
		if (address == InternalRomDisable) _internalRomEnabled &= !value;
		else if (_cgbMode) WriteCgbRegister(address, value);
	}
	else
	{
		_highRam[address - HighRam] = value;
	}
}

void MemoryMap::SetCgbMode(bool cgbMode)
{
	_cgbMode = cgbMode;
	if (!cgbMode)
	{
		_workRamBankOffset = WorkRamBankSize;
		_workRamBankRegister = 0;
	}
}

void MemoryMap::ApplyCgbPostBootState()
{
	_internalRomEnabled = false;
}

unsigned char MemoryMap::ReadCgbRegister(unsigned short address) const
{
	if (address >= UndocumentedRegisters::FirstRegister && address <= UndocumentedRegisters::LastRegister)
	{
		return _undocumentedRegisters.Read(address);
	}

	switch (address)
	{
	case Graphics::VramBankRegister:
		return _graphics->ReadCgbRegister(address);

	case InfraredRegister:
		return _infraredPort.Read();

	case WorkRamBankRegister:
		return 0xf8 | _workRamBankRegister;

	// KEY0 is written only by the CGB boot ROM, which locks it as it hands over (Pan Docs), so after the boot ROM
	// it reads $FF and ignores writes, as in SameBoy
	case CpuModeRegister:
	default:
		return 0xff;
	}
}

void MemoryMap::WriteCgbRegister(unsigned short address, unsigned char value)
{
	if (address >= UndocumentedRegisters::FirstRegister && address <= UndocumentedRegisters::LastRegister)
	{
		_undocumentedRegisters.Write(address, value);
		return;
	}

	switch (address)
	{
	case Graphics::VramBankRegister:
		_graphics->WriteCgbRegister(address, value);
		break;

	case InfraredRegister:
		_infraredPort.Write(value);
		break;

	case WorkRamBankRegister:
	{
		// Writing 0 selects bank 1 (Pan Docs)
		_workRamBankRegister = value & 7;
		auto bank = _workRamBankRegister ? _workRamBankRegister : 1;
		_workRamBankOffset = bank * WorkRamBankSize;
		break;
	}

	default:
		break;
	}
}

void MemoryMap::WriteState(StateWriter& writer) const
{
	writer.BeginSection("MEM ");

	// The DMG's 8 KB of work RAM, then in CGB states the CGB's further banks and registers
	writer.WriteBytes(_workRam, RamBankSize);
	writer.WriteBytes(_highRam, sizeof(_highRam));
	writer.WriteBool(_internalRomEnabled);

	if (_cgbMode)
	{
		writer.WriteBytes(_workRam + RamBankSize, sizeof(_workRam) - RamBankSize);
		writer.WriteU8(_workRamBankRegister);
		writer.WriteU8(_infraredPort.Value());
		writer.WriteBytes(_undocumentedRegisters.Data(), UndocumentedRegisters::Count);
	}

	writer.EndSection();
}

void MemoryMap::ReadState(StateReader& reader)
{
	reader.BeginSection("MEM ");
	reader.ReadBytes(_workRam, RamBankSize);
	reader.ReadBytes(_highRam, sizeof(_highRam));
	_internalRomEnabled = reader.ReadBool();

	// A DMG state leaves the CGB's banks and registers as at power-on
	std::fill(_workRam + RamBankSize, std::end(_workRam), 0);
	_workRamBankRegister = 0;
	unsigned char infrared = 0;
	unsigned char undocumented[UndocumentedRegisters::Count]{};

	if (_cgbMode)
	{
		reader.ReadBytes(_workRam + RamBankSize, sizeof(_workRam) - RamBankSize);
		_workRamBankRegister = reader.ReadU8() & 7;
		infrared = reader.ReadU8();
		reader.ReadBytes(undocumented, sizeof(undocumented));
	}

	_infraredPort.Write(infrared);
	for (auto i = 0; i < UndocumentedRegisters::Count; i++)
	{
		_undocumentedRegisters.Write(static_cast<unsigned short>(UndocumentedRegisters::FirstRegister + i), undocumented[i]);
	}

	_workRamBankOffset = (_workRamBankRegister ? _workRamBankRegister : 1) * WorkRamBankSize;
	reader.EndSection();
}
