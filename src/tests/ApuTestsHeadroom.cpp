#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Loud music must not clip. After the DC-blocking high-pass filter, a full-scale jump between the DAC extremes
// swings up to twice the DAC range, so the output needs that much headroom.

TEST(ApuHeadroomTest, LoudFourChannelMusicNeverClips)
{
	Apu apu{ 44100 };
	apu.WriteRegister(Nr52, 0x80);
	apu.WriteRegister(Nr50, 0x77);
	apu.WriteRegister(Nr51, 0xff);

	// Wave RAM alternating between the extremes, played at full volume
	apu.WriteRegister(Nr30, 0x00);

	for (unsigned short address = WaveRam; address < WaveRam + 16; address++)
	{
		apu.WriteRegister(address, address & 1 ? 0x00 : 0xff);
	}

	apu.WriteRegister(Nr30, 0x80);
	apu.WriteRegister(Nr32, 0x20);

	srand(1);
	std::vector<short> samples;
	auto clipped = 0;
	auto total = 0;

	// Random notes on all four channels at full volume, ten seconds of play
	for (auto note = 0; note < 400; note++)
	{
		auto frequency = 1024 + rand() % 1000;
		apu.WriteRegister(Nr11, (rand() % 4) << 6);
		apu.WriteRegister(Nr12, 0xf0);
		apu.WriteRegister(Nr13, frequency & 0xff);
		apu.WriteRegister(Nr14, 0x80 | frequency >> 8);
		apu.WriteRegister(Nr21, (rand() % 4) << 6);
		apu.WriteRegister(Nr22, 0xf0);
		apu.WriteRegister(Nr23, (frequency / 2) & 0xff);
		apu.WriteRegister(Nr24, 0x80 | (frequency / 2) >> 8);
		apu.WriteRegister(Nr33, (frequency / 4) & 0xff);
		apu.WriteRegister(Nr34, 0x80 | (frequency / 4) >> 8);
		apu.WriteRegister(Nr42, 0xf0);
		apu.WriteRegister(Nr43, rand() % 0x80);
		apu.WriteRegister(Nr44, 0x80);

		apu.RunCycles(Apu::CpuClockHz / 40);
		apu.TakeSamples(samples);

		for (auto sample : samples)
		{
			total++;

			if (sample == 32767 || sample == -32768)
			{
				clipped++;
			}
		}
	}

	EXPECT_GT(total, 0);
	EXPECT_EQ(0, clipped) << "of " << total << " samples";
}
