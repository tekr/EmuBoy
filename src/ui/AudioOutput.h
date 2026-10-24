#pragma once
#include "SFML/Audio.hpp"
#include <deque>
#include <mutex>
#include <vector>

// Plays the emulator's interleaved stereo samples. SFML pulls data on its own audio thread, so the buffer is shared
// under a mutex. Buffered audio is capped by dropping the oldest samples, so latency can't build up as the wall clock
// and the emulated audio clock drift apart; an underrun plays silence.
class AudioOutput : public sf::SoundStream
{
	static const int Channels = 2;
	static const int ChunkFrames = 1024;

	std::mutex _mutex;
	std::deque<short> _buffer;
	std::vector<short> _chunk;
	size_t _maxBuffered;

	bool onGetData(Chunk& data) override;
	void onSeek(sf::Time) override {}

public:
	explicit AudioOutput(int sampleRate);
	~AudioOutput() override;

	void Push(const std::vector<short>& samples);
};
