#include "stdafx.h"
#include "RealTimeClock.h"

namespace
{
	const unsigned char RegisterMasks[] = { 0x3f, 0x3f, 0x1f, 0xff, 0xc1 };

	const int SecondsPerMinute = 60;
	const int MinutesPerHour = 60;
	const int HoursPerDay = 24;
	const int DayCounterRange = 512;

	// Counters only roll over (and carry) on reaching their limit. One set beyond it counts on until its bits
	// overflow, then wraps to 0 without carrying
	bool IncrementCounter(unsigned char& counter, unsigned char mask, int limit)
	{
		counter = (counter + 1) & mask;
		if (counter != limit) return false;

		counter = 0;
		return true;
	}
}

bool RealTimeClock::CountersInRange() const
{
	auto& counters = _state.Counters;
	return counters[Seconds] < SecondsPerMinute && counters[Minutes] < MinutesPerHour && counters[Hours] < HoursPerDay;
}

void RealTimeClock::Tick()
{
	auto& counters = _state.Counters;

	if (!IncrementCounter(counters[Seconds], RegisterMasks[Seconds], SecondsPerMinute)) return;
	if (!IncrementCounter(counters[Minutes], RegisterMasks[Minutes], MinutesPerHour)) return;
	if (!IncrementCounter(counters[Hours], RegisterMasks[Hours], HoursPerDay)) return;

	if (++counters[DaysLow] != 0) return;

	// Day 511 rolls over to 0 and sets the carry bit, which stays set until the game clears it
	if (counters[DaysHigh] & DayHighBit) counters[DaysHigh] = (counters[DaysHigh] & ~DayHighBit) | DayCarryBit;
	else counters[DaysHigh] |= DayHighBit;
}

void RealTimeClock::RunCycles(int cycles)
{
	if (IsHalted()) return;

	_state.CyclesIntoSecond += cycles;

	while (_state.CyclesIntoSecond >= CyclesPerSecond)
	{
		_state.CyclesIntoSecond -= CyclesPerSecond;
		Tick();
	}
}

void RealTimeClock::AdvanceSeconds(uint64_t seconds)
{
	if (IsHalted()) return;

	// Out-of-range counters need stepping through their unusual wrapping, which takes at most a few hours
	for (; seconds > 0 && !CountersInRange(); seconds--) Tick();

	auto& counters = _state.Counters;
	auto days = counters[DaysLow] | (counters[DaysHigh] & DayHighBit) << 8;
	auto total = seconds + counters[Seconds] + SecondsPerMinute * (counters[Minutes] + MinutesPerHour * (counters[Hours] +
		static_cast<uint64_t>(HoursPerDay) * days));

	counters[Seconds] = total % SecondsPerMinute;
	total /= SecondsPerMinute;
	counters[Minutes] = total % MinutesPerHour;
	total /= MinutesPerHour;
	counters[Hours] = total % HoursPerDay;
	total /= HoursPerDay;

	if (total >= DayCounterRange) counters[DaysHigh] |= DayCarryBit;
	days = total % DayCounterRange;

	counters[DaysLow] = days & 0xff;
	counters[DaysHigh] = (counters[DaysHigh] & ~DayHighBit) | days >> 8;
}

void RealTimeClock::WriteLatch(unsigned char value)
{
	if (_state.LatchArmed && value == 1) _state.Latched = _state.Counters;
	_state.LatchArmed = value == 0;
}

void RealTimeClock::WriteRegister(int reg, unsigned char value)
{
	value &= RegisterMasks[reg];

	// Writing the seconds resets the clock's sub-second divider
	if (reg == Seconds) _state.CyclesIntoSecond = 0;

	_state.Counters[reg] = value;
	_state.Latched[reg] = value;
}
