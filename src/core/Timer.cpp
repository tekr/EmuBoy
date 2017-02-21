#include "stdafx.h"
#include "Timer.h"
#include "Cpu.h"

const int Timer::ModeFreqs[] = { 4096, 262144, 65536, 16384 };

Timer::Timer(): _isRunning(false), _divisorMode(0), _cyclesToNextCounterInc(0)
{
}

void Timer::RunCycles(int numCycles)
{
	auto cyclesToNextEvent = GetCyclesToNextEvent();

	for (; cyclesToNextEvent <= numCycles; numCycles -= cyclesToNextEvent, cyclesToNextEvent = GetCyclesToNextEvent())
	{
		for (_cyclesToNextDivInc -= cyclesToNextEvent; _cyclesToNextDivInc <= 0; _cyclesToNextDivInc += CyclesPerDivInc)
		{
			_registers[DivReg]++;
		}

		if (_isRunning)
		{
			for (_cyclesToNextCounterInc -= cyclesToNextEvent; _cyclesToNextCounterInc <= 0; _cyclesToNextCounterInc += GetCyclesPerCounterInc())
			{
				if (++_registers[CounterReg] == 0)
				{
					_registers[CounterReg] = _registers[ModuloReg];
					_cpu->RequestInterrupt(InterruptFlags::TimerInt);
				}
			}
		}
	}

	_cyclesToNextDivInc -= numCycles;
	if (_isRunning) _cyclesToNextCounterInc -= numCycles;
}

Timer::~Timer()
{
}
