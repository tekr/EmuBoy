#include "stdafx.h"
#include "Serial.h"
#include "Cpu.h"
#include "StateStream.h"

Serial::Serial() : _cpu(nullptr), _data(0), _control(0), _bitsLeft(0), _clockPhase(PowerOnClocks), _sentByte(0)
{
}

void Serial::WriteRegister(int reg, unsigned char value)
{
	if (reg == DataReg)
	{
		_data = value;
		return;
	}

	_control = value & (TransferStart | InternalClock);

	// With an external clock the transfer waits for a link partner that never arrives
	if (_control == (TransferStart | InternalClock))
	{
		_sentByte = _data;
		_bitsLeft = BitsPerTransfer;
	}
	else
	{
		_bitsLeft = 0;
	}
}

void Serial::ShiftBit()
{
	// Bits go out from the top of SB while the disconnected line shifts 1s in at the bottom
	_data = static_cast<unsigned char>(_data << 1 | 1);

	if (--_bitsLeft > 0) return;

	_control &= ~TransferStart;
	if (_cpu != nullptr) _cpu->RequestInterrupt(InterruptFlags::SerialInt);
	if (_onByteSent) _onByteSent(_sentByte);
}

void Serial::WriteState(StateWriter& writer) const
{
	writer.BeginSection("SERL");
	writer.WriteU8(_data);
	writer.WriteU8(_control);
	writer.WriteInt(_bitsLeft);
	writer.WriteInt(_clockPhase);
	writer.WriteU8(_sentByte);
	writer.EndSection();
}

void Serial::ReadState(StateReader& reader)
{
	reader.BeginSection("SERL");
	_data = reader.ReadU8();
	_control = reader.ReadU8() & (TransferStart | InternalClock);
	_bitsLeft = reader.ReadInt(0, BitsPerTransfer);
	_clockPhase = reader.ReadInt(0, CyclesPerBit - 1);
	_sentByte = reader.ReadU8();
	reader.EndSection();
}
