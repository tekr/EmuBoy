#pragma once
#include <functional>

class Cpu;

// Serial port (SB at 0xff01, SC at 0xff02). Nothing is ever connected to the link port, so only transfers
// clocked by the Game Boy itself complete, and the bits shifted in are all 1s.
// See https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html
class Serial
{
public:
	static const int DataReg = 0;
	static const int ControlReg = 1;

	// The internal clock shifts one bit at 8192 Hz
	static const int CyclesPerBit = 512;
	static const int BitsPerTransfer = 8;

private:
	static const unsigned char TransferStart = 0x80;
	static const unsigned char InternalClock = 0x01;

	// Bits 1-6 of SC are unused and read as 1
	static const unsigned char ControlUnusedBits = 0x7e;

	Cpu* _cpu;
	std::function<void(unsigned char)> _onByteSent;

	unsigned char _data;
	unsigned char _control;

	int _bitsLeft;
	int _cyclesToNextBit;

	// SB as it was when the transfer started; SB itself fills with the received bits
	unsigned char _sentByte;

	bool IsTransferring() const { return _bitsLeft > 0; }
	void ShiftBit();

public:
	Serial();

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	// Called with each byte the Game Boy sends, once its transfer completes
	void SetByteSentHandler(std::function<void(unsigned char)> handler) { _onByteSent = std::move(handler); }

	// T-cycles (4194304 Hz)
	void RunCycles(int cycles)
	{
		if (!IsTransferring()) return;

		for (_cyclesToNextBit -= cycles; IsTransferring() && _cyclesToNextBit <= 0; _cyclesToNextBit += CyclesPerBit)
		{
			ShiftBit();
		}
	}

	unsigned char ReadRegister(int reg) const { return reg == DataReg ? _data : _control | ControlUnusedBits; }
	void WriteRegister(int reg, unsigned char value);
};
