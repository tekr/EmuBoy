#include "stdafx.h"
#include "AudioOutput.h"
#include <algorithm>

AudioOutput::AudioOutput(int sampleRate) : _chunk(ChunkFrames * Channels), _maxBuffered(sampleRate * Channels * MaxLatencyMs / 1000)
{
	initialize(Channels, sampleRate);
}

AudioOutput::~AudioOutput()
{
	// Stop the audio thread before the buffer it reads from goes away
	stop();
}

void AudioOutput::Push(const std::vector<short>& samples)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_buffer.insert(_buffer.end(), samples.begin(), samples.end());

	if (_buffer.size() > _maxBuffered)
	{
		// Drop whole frames so left and right stay paired
		auto excess = (_buffer.size() - _maxBuffered + Channels - 1) / Channels * Channels;
		_buffer.erase(_buffer.begin(), _buffer.begin() + excess);
	}
}

bool AudioOutput::onGetData(Chunk& data)
{
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto count = std::min(_buffer.size(), _chunk.size());
		std::copy(_buffer.begin(), _buffer.begin() + count, _chunk.begin());
		_buffer.erase(_buffer.begin(), _buffer.begin() + count);
		std::fill(_chunk.begin() + count, _chunk.end(), 0);
	}

	data.samples = _chunk.data();
	data.sampleCount = _chunk.size();
	return true;
}
