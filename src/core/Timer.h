#pragma once
#include <algorithm>

class Cpu;

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

	Cpu* _cpu;

	// Increments every cycle; DIV is its upper byte
	unsigned short _divider;

	// DIV's slot is unused as DIV is read from the divider
	unsigned char _registers[3];
	bool _isRunning;
	int _divisorMode;

	int GetCyclesPerCounterInc() const { return 2 << ModeBits[_divisorMode]; }
	bool GetCounterSignal() const { return _isRunning && (_divider & GetCyclesPerCounterInc() >> 1) != 0; }

	void IncCounter();

public:
	Timer();

	int GetCyclesToNextEvent() const
	{
		auto cyclesToNextDivInc = 0x100 - (_divider & 0xff);
		return _isRunning ? std::min(cyclesToNextDivInc, GetCyclesPerCounterInc() - (_divider & (GetCyclesPerCounterInc() - 1))) : cyclesToNextDivInc;
	}

	void RunCycles(int numCycles);

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	void WriteRegister(int address, unsigned char value)
	{
		// Resetting the divider, disabling the timer or changing the mode can cause a falling edge
		auto oldSignal = GetCounterSignal();

		if (address == DivReg) _divider = 0;
		else if (address < ControlReg) _registers[address] = value;
		else
		{
			_divisorMode = value & 3;
			_isRunning = (value & 4) != 0;
		}

		if (oldSignal && !GetCounterSignal()) IncCounter();
	}

	unsigned char ReadRegister(int address)
	{
		return address == DivReg ? _divider >> 8 : address < ControlReg ? _registers[address] : _divisorMode | (_isRunning ? 4 : 0);
	}
};

