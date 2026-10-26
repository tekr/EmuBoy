#include "FramePacer.h"

FramePacer::FramePacer(int64_t frameTime, int64_t start) : _frameTime{ frameTime }, _nextFrame{ start + frameTime },
	_nextPresent{ start }
{
}

int64_t FramePacer::FrameDone(int64_t now, int speed)
{
	auto interval = _frameTime / speed;
	auto wait = _nextFrame > now ? _nextFrame - now : 0;

	if (now - _nextFrame > interval * MaxFramesBehind) _nextFrame = now;
	_nextFrame += interval;

	return wait;
}

bool FramePacer::ShouldPresent(int64_t now, int speed)
{
	if (speed > 1 && now < _nextPresent) return false;

	_nextPresent += _frameTime;
	if (_nextPresent <= now) _nextPresent = now + _frameTime;
	return true;
}
