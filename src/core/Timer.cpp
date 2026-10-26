#include "stdafx.h"
#include "Timer.h"
#include "Cpu.h"

const int Timer::ModeBits[] = { 9, 3, 5, 7 };

Timer::Timer(): _cpu(nullptr), _divider(0), _registers{}, _isRunning(false), _divisorMode(0)
{
}

void Timer::IncCounter()
{
	if (++_registers[CounterReg] == 0)
	{
		_registers[CounterReg] = _registers[ModuloReg];
		_cpu->RequestInterrupt(InterruptFlags::TimerInt);
	}
}

void Timer::RunCycles(int numCycles)
{
	// Count the falling edges of the selected divider bit rather than stepping through each cycle
	auto counterIncs = _isRunning ? (_divider + numCycles) / GetCyclesPerCounterInc() - _divider / GetCyclesPerCounterInc() : 0;
	_divider = static_cast<unsigned short>(_divider + numCycles);

	for (; counterIncs > 0; counterIncs--) IncCounter();
}
