#pragma once

class Cpu;
class StateWriter;
class StateReader;

// CPU HALT: Timer/Div keep running
// CPU STOP: Timer/Div stop running

class Timer
{
	// Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html, the counter increments on the falling
	// edge of one bit of the internal divider (selected by the mode) ANDed with the timer enable bit
	static const int ModeBits[];

	static const int DivReg = 0;
	static const int CounterReg = 1;
	static const int ModuloReg = 2;
	static const int ControlReg = 3;

	// After TIMA overflows it holds 0 for one machine cycle before being reloaded from TMA
	static const int ReloadDelayCycles = 4;

	// The divider has counted a machine cycle by the time the CPU fetches its first opcode, which puts DIV in the
	// phase mooneye-test-suite's boot_div-dmgABCmgb expects when the DMG boot ROM hands over
	static const unsigned short PowerOnDivider = 4;

	Cpu* _cpu;

	// Increments every cycle; DIV is its upper byte
	unsigned short _divider;

	// DIV's slot is unused as DIV is read from the divider
	unsigned char _registers[3];
	bool _isRunning;
	int _divisorMode;

	// Cycles until an overflowed TIMA is reloaded, or 0 if no reload is pending
	int _cyclesToReload;

	// Cycles left of the machine cycle in which TIMA was reloaded, or 0 outside it
	int _cyclesLeftInReload;

	int GetCyclesPerCounterInc() const { return 2 << ModeBits[_divisorMode]; }
	bool GetCounterSignal() const { return _isRunning && (_divider & GetCyclesPerCounterInc() >> 1) != 0; }
	int GetCyclesToNextEvent() const;

	void IncCounter();
	void Reload();
	void WriteCounter(unsigned char value);
	void WriteModulo(unsigned char value);

public:
	Timer();

	void RunCycles(int numCycles);

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	void WriteRegister(int address, unsigned char value);

	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);

	unsigned char ReadRegister(int address) const
	{
		return address == DivReg ? _divider >> 8 : address < ControlReg ? _registers[address] : 0xf8 | _divisorMode | (_isRunning ? 4 : 0);
	}
};
