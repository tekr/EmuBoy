#pragma once
#include <functional>

class Cpu;
class StateWriter;
class StateReader;

// Serial port (SB at 0xff01, SC at 0xff02). Nothing is ever connected to the link port, so only transfers
// clocked by the Game Boy itself complete, and the bits shifted in are all 1s.
// See https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html
class Serial
{
public:
	static const int DataReg = 0;
	static const int ControlReg = 1;

	// The internal clock shifts one bit at 8192 Hz, or in CGB mode with SC bit 1 set, 32 times as fast at 262144 Hz
	// (https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html#ff02--sc-serial-transfer-control)
	static const int CyclesPerBit = 512;
	static const int FastCyclesPerBit = 16;
	static const int BitsPerTransfer = 8;

	static const int PowerOnClocks = 4;

private:
	static const unsigned char TransferStart = 0x80;
	static const unsigned char InternalClock = 0x01;
	static const unsigned char FastClock = 0x02;

	// Bits 1-6 of SC are unused and read as 1, except that bit 1 is the clock speed in CGB mode
	static const unsigned char ControlUnusedBits = 0x7e;
	static const unsigned char CgbControlUnusedBits = 0x7c;

	bool _cgbMode{ false };

	Cpu* _cpu;
	std::function<void(unsigned char)> _onByteSent;

	unsigned char _data;
	unsigned char _control;

	int _bitsLeft;

	// Clocks into the current bit period of the internal clock, which is divided down from power-on, so bits shift
	// on its period boundaries rather than a fixed time after a transfer starts (mooneye-test-suite's
	// boot_sclk_align-dmgABCmgb)
	int _clockPhase;

	// SB as it was when the transfer started; SB itself fills with the received bits
	unsigned char _sentByte;

	bool IsTransferring() const { return _bitsLeft > 0; }
	int BitPeriod() const { return _cgbMode && (_control & FastClock) ? FastCyclesPerBit : CyclesPerBit; }
	unsigned char ControlBits() const { return TransferStart | InternalClock | (_cgbMode ? FastClock : 0); }
	void ShiftBit();

public:
	Serial();

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	// CGB mode adds SC's clock speed bit
	void SetCgbMode(bool cgbMode) { _cgbMode = cgbMode; }

	// The CGB boot ROM leaves SC reading $7F: the internal clock selected at the fast speed, with no transfer
	void ApplyCgbPostBootState() { _control = InternalClock | FastClock; }

	// Called with each byte the Game Boy sends, once its transfer completes
	void SetByteSentHandler(std::function<void(unsigned char)> handler) { _onByteSent = std::move(handler); }

	// T-cycles (4194304 Hz)
	void RunCycles(int cycles)
	{
		// The phase counts through the slow clock's period, which the fast clock's divides exactly
		auto phase = _clockPhase + cycles;
		_clockPhase = phase % CyclesPerBit;
		if (!IsTransferring()) return;

		auto bitPeriod = BitPeriod();
		for (auto periods = phase / bitPeriod - (phase - cycles) / bitPeriod; periods > 0 && IsTransferring(); periods--)
		{
			ShiftBit();
		}
	}

	unsigned char ReadRegister(int reg) const
	{
		return reg == DataReg ? _data : _control | (_cgbMode ? CgbControlUnusedBits : ControlUnusedBits);
	}
	void WriteRegister(int reg, unsigned char value);

	// Save states. The byte-sent handler belongs to the front end, so isn't part of them
	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);
};
