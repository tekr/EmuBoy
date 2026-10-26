#include "FramePacer.h"

FramePacer::FramePacer(int64_t clockHz, int64_t frameClocks, int64_t start, uint64_t startClocks) :
	_clockHz{ clockHz }, _frameTime{ 1000000 * frameClocks / clockHz }, _speed{ 1 }, _nextPresent{ start }
{
	Restart(start, startClocks);
}

void FramePacer::Restart(int64_t now, uint64_t clocks)
{
	_pacedFrom = now;
	_pacedFromClocks = clocks;
	_lastClocks = clocks;
	_lastDue = now;
}

int64_t FramePacer::DueTime(uint64_t clocks) const
{
	// Whole seconds first, so the microsecond count can't overflow however long the schedule has run
	auto rate = static_cast<uint64_t>(_clockHz * _speed);
	auto elapsed = clocks - _pacedFromClocks;
	auto micros = elapsed / rate * 1000000 + elapsed % rate * 1000000 / rate;

	return _pacedFrom + static_cast<int64_t>(micros);
}

int64_t FramePacer::FrameDone(int64_t now, uint64_t clocks, int speed)
{
	// Clocks counting from an earlier point can only mean a new emulator, which starts a new schedule
	if (clocks < _lastClocks) Restart(now, clocks);

	// A change of speed applies to clocks from here on, not to those already run
	if (speed != _speed)
	{
		_pacedFrom = _lastDue;
		_pacedFromClocks = _lastClocks;
		_speed = speed;
	}

	auto due = DueTime(clocks);
	auto wait = due > now ? due - now : 0;

	if (now - due > _frameTime / speed * MaxFramesBehind)
	{
		Restart(now, clocks);
	}
	else
	{
		_lastClocks = clocks;
		_lastDue = due;
	}

	return wait;
}

int64_t FramePacer::Paused(int64_t now, uint64_t clocks)
{
	Restart(now + _frameTime, clocks);
	return _frameTime;
}

bool FramePacer::ShouldPresent(int64_t now, int speed)
{
	if (speed > 1 && now < _nextPresent) return false;

	_nextPresent += _frameTime;
	if (_nextPresent <= now) _nextPresent = now + _frameTime;
	return true;
}
