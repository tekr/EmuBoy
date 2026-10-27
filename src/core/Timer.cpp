#include "stdafx.h"
#include "Timer.h"
#include "Cpu.h"
#include <algorithm>
#include <climits>

const int Timer::ModeBits[] = { 9, 3, 5, 7 };

Timer::Timer(): _cpu(nullptr), _divider(0), _registers{}, _isRunning(false), _divisorMode(0), _cyclesToReload(0),
	_cyclesLeftInReload(0)
{
}

int Timer::GetCyclesToNextEvent() const
{
	auto period = GetCyclesPerCounterInc();
	auto cycles = _isRunning ? period - (_divider & (period - 1)) : INT_MAX;

	if (_cyclesToReload > 0) cycles = std::min(cycles, _cyclesToReload);
	if (_cyclesLeftInReload > 0) cycles = std::min(cycles, _cyclesLeftInReload);

	return cycles;
}

void Timer::IncCounter()
{
	// Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html#timer-overflow-behavior, TIMA reads 0 until the
	// reload a machine cycle later
	if (++_registers[CounterReg] == 0) _cyclesToReload = ReloadDelayCycles;
}

void Timer::Reload()
{
	_registers[CounterReg] = _registers[ModuloReg];
	_cyclesLeftInReload = ReloadDelayCycles;
	_cpu->RequestInterrupt(InterruptFlags::TimerInt);
}

void Timer::RunCycles(int numCycles)
{
	// Step from one event (counter increment, reload or end of the reload cycle) to the next
	while (numCycles > 0)
	{
		auto cycles = std::min(numCycles, GetCyclesToNextEvent());
		numCycles -= cycles;
		_divider = static_cast<unsigned short>(_divider + cycles);

		if (_cyclesLeftInReload > 0) _cyclesLeftInReload -= cycles;

		if (_cyclesToReload > 0 && (_cyclesToReload -= cycles) == 0) Reload();
		if (_isRunning && (_divider & (GetCyclesPerCounterInc() - 1)) == 0) IncCounter();
	}
}

void Timer::WriteCounter(unsigned char value)
{
	// A write in the cycle TIMA is reloaded is overwritten by the reload. A write in the cycle before cancels it
	if (_cyclesLeftInReload > 0) return;

	_registers[CounterReg] = value;
	_cyclesToReload = 0;
}

void Timer::WriteModulo(unsigned char value)
{
	_registers[ModuloReg] = value;

	// In the cycle TIMA is reloaded, it's loaded with the new TMA too
	if (_cyclesLeftInReload > 0) _registers[CounterReg] = value;
}

void Timer::WriteRegister(int address, unsigned char value)
{
	// Resetting the divider, disabling the timer or changing the mode can cause a falling edge
	auto oldSignal = GetCounterSignal();

	switch (address)
	{
	case DivReg:
		_divider = 0;
		break;

	case CounterReg:
		WriteCounter(value);
		break;

	case ModuloReg:
		WriteModulo(value);
		break;

	default:
		_divisorMode = value & 3;
		_isRunning = (value & 4) != 0;
	}

	if (oldSignal && !GetCounterSignal()) IncCounter();
}
