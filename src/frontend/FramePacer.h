#pragma once
#include <cstdint>

// Paces frames to the Game Boy's frame rate against a running schedule, so sleep overshoot doesn't accumulate the
// way it does with a fixed sleep per frame. Times are in microseconds, from any fixed starting point
class FramePacer
{
	// How many frames behind schedule the pacer may fall (e.g. while the window is dragged) before it gives up
	// catching up, which would run frames back to back
	static const int MaxFramesBehind = 4;

	int64_t _frameTime;
	int64_t _nextFrame;
	int64_t _nextPresent;

public:
	// The first frame is due one frame time after start
	explicit FramePacer(int64_t frameTime, int64_t start = 0);

	// Records that a frame finished at now and returns how long to wait before starting the next. speed is how
	// many frames run in one frame time: more than 1 to fast-forward
	int64_t FrameDone(int64_t now, int speed);

	// Whether a frame finished at now should be shown. At normal speed every frame is; when fast-forwarding, frames
	// are shown at no more than the normal frame rate, as showing more would only cost time
	bool ShouldPresent(int64_t now, int speed);
};
