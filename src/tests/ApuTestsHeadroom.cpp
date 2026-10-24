#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>
#include "../core/Apu.h"

// Loud music must not clip. After the DC-blocking high-pass filter, a full-scale jump between the DAC extremes
// swings up to twice the DAC range, so the output needs that much headroom.

TEST(ApuHeadroomTest, LoudFourChannelMusicNeverClips)
{
	Apu apu{ 44100 };
	apu.WriteRegister(0xff26, 0x80);
	apu.WriteRegister(0xff24, 0x77);
	apu.WriteRegister(0xff25, 0xff);

	// Wave RAM alternating between the extremes, played at full volume
	apu.WriteRegister(0xff1a, 0x00);
	for (unsigned short address = 0xff30; address < 0xff40; address++) apu.WriteRegister(address, address & 1 ? 0x00 : 0xff);
	apu.WriteRegister(0xff1a, 0x80);
	apu.WriteRegister(0xff1c, 0x20);

	srand(1);
	std::vector<short> samples;
	auto clipped = 0;
	auto total = 0;

	// Random notes on all four channels at full volume, ten seconds of play
	for (auto note = 0; note < 400; note++)
	{
		auto frequency = 1024 + rand() % 1000;
		apu.WriteRegister(0xff11, (rand() % 4) << 6);
		apu.WriteRegister(0xff12, 0xf0);
		apu.WriteRegister(0xff13, frequency & 0xff);
		apu.WriteRegister(0xff14, 0x80 | frequency >> 8);
		apu.WriteRegister(0xff16, (rand() % 4) << 6);
		apu.WriteRegister(0xff17, 0xf0);
		apu.WriteRegister(0xff18, (frequency / 2) & 0xff);
		apu.WriteRegister(0xff19, 0x80 | (frequency / 2) >> 8);
		apu.WriteRegister(0xff1d, (frequency / 4) & 0xff);
		apu.WriteRegister(0xff1e, 0x80 | (frequency / 4) >> 8);
		apu.WriteRegister(0xff21, 0xf0);
		apu.WriteRegister(0xff22, rand() % 0x80);
		apu.WriteRegister(0xff23, 0x80);

		apu.RunCycles(Apu::CpuClockHz / 40);
		apu.TakeSamples(samples);

		for (auto sample : samples)
		{
			total++;
			if (sample == 32767 || sample == -32768) clipped++;
		}
	}

	EXPECT_GT(total, 0);
	EXPECT_EQ(0, clipped) << "of " << total << " samples";
}
