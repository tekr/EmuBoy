#pragma once
#include "../core/Cpu.h"
#include "../core/Timer.h"

// Clocked hardware for CPU tests that runs the timer, so timer register accesses happen at the machine cycle the
// instruction makes them. Counts the cycles it runs, and the cycles of time passing while STOP has stopped the
// system clock (which don't run the timer)
class TimerTestHardware : public ClockedHardware
{
	Timer& _timer;

public:
	explicit TimerTestHardware(Timer& timer) : _timer(timer) {}

	int Cycles = 0;
	int StoppedCycles = 0;

	void RunCycles(int cycles) override
	{
		Cycles += cycles;
		_timer.RunCycles(cycles);
	}

	void RunStoppedCycles(int cycles) override { StoppedCycles += cycles; }
};
