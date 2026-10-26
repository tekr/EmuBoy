#pragma once
#include <cstdint>

// Paces emulation to the Game Boy's clock against a running schedule, so sleep overshoot doesn't accumulate the way
// it does with a fixed sleep per frame. Each frame is due at the wall-clock time matching the emulated clocks run
// since a reference point, rather than one frame time after the last: frames are usually a frame time apart, but the
// one in which the LCD is turned on lasts up to nearly two. Times are in microseconds, from any fixed starting point
class FramePacer
{
	// How many frames behind schedule the pacer may fall (e.g. while the window is dragged) before it gives up
	// catching up, which would run frames back to back
	static const int MaxFramesBehind = 4;

	int64_t _clockHz;
	int64_t _frameTime;

	// The schedule: clocks _pacedFromClocks were due at _pacedFrom, at speed _speed
	int64_t _pacedFrom;
	uint64_t _pacedFromClocks;
	int _speed;

	// The clocks the last frame ended at, and when they were due
	uint64_t _lastClocks;
	int64_t _lastDue;

	int64_t _nextPresent;

	// When clocks are due on the current schedule
	int64_t DueTime(uint64_t clocks) const;

public:
	// clockHz emulated clocks run in a second at normal speed, and a frame is frameClocks long. Clocks startClocks
	// are due at start
	FramePacer(int64_t clockHz, int64_t frameClocks, int64_t start = 0, uint64_t startClocks = 0);

	// Restarts the schedule so that clocks are due at now, e.g. after a reset, when a new emulator's clocks start
	// again from 0
	void Restart(int64_t now, uint64_t clocks);

	// Records that a frame ended at now, with clocks emulated in all, and returns how long to wait before starting
	// the next. speed is how many times faster than the Game Boy to run: more than 1 to fast-forward
	int64_t FrameDone(int64_t now, uint64_t clocks, int speed);

	// Records that a frame time passed at now without emulating, as while paused, and returns how long to wait
	// before the next. Emulation resumes with its next frame due a frame time after that
	int64_t Paused(int64_t now, uint64_t clocks);

	// Whether a frame finished at now should be shown. At normal speed every frame is; when fast-forwarding, frames
	// are shown at no more than the normal frame rate, as showing more would only cost time
	bool ShouldPresent(int64_t now, int speed);
};
