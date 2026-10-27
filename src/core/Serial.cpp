#include "stdafx.h"
#include "Serial.h"
#include "Cpu.h"

Serial::Serial() : _cpu(nullptr), _data(0), _control(0), _bitsLeft(0), _cyclesToNextBit(0), _sentByte(0)
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
		_cyclesToNextBit = CyclesPerBit;
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
