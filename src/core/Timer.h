#pragma once
#include <algorithm>

class Cpu;

// CPU HALT: Timer/Div keep running
// CPU STOP: Timer/Div stop running

class Timer
{
	static const int ClockFreq = 4194304;
	static const int DivFreq = 16384;

	static const int ModeFreqs[];

	static const int DivReg = 0;
	static const int CounterReg = 1;
	static const int ModuloReg = 2;
	static const int ControlReg = 3;

	static constexpr int CyclesPerDivInc = ClockFreq / DivFreq;

	Cpu* _cpu;

	unsigned char _registers[3];
	bool _isRunning;
	int _divisorMode;

	int _cyclesToNextDivInc = CyclesPerDivInc;
	int _cyclesToNextCounterInc;

	int GetCyclesPerCounterInc() const { return ClockFreq / ModeFreqs[_divisorMode];	}

public:
	Timer();

	int GetCyclesToNextEvent() const { return std::max(0, _isRunning ? std::min(_cyclesToNextCounterInc, _cyclesToNextDivInc) : _cyclesToNextDivInc); }

	void RunCycles(int numCycles);

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	void WriteRegister(int address, unsigned char value)
	{
		if (address == DivReg) value = 0;

		if (address < ControlReg) _registers[address] = value;
		else
		{
			auto newMode = value & 3;
			if (newMode != _divisorMode)
			{
				_divisorMode = newMode;
				_cyclesToNextCounterInc = GetCyclesPerCounterInc();
			}

			auto newIsRunning = (value & 4) != 0;
			if (newIsRunning != _isRunning)
			{
				_isRunning = newIsRunning;
				_cyclesToNextCounterInc = GetCyclesPerCounterInc();
			}
		}
	}

	unsigned char ReadRegister(int address)
	{
		return address < ControlReg ? _registers[address] : _divisorMode | (_isRunning ? 4 : 0);
	}

	~Timer();
};

