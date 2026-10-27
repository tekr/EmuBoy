#include "stdafx.h"
#include <gtest/gtest.h>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// The APU in CGB mode, which differs from the DMG's in what blargg's cgb_sound checks against dmg_sound (and SameBoy's
// apu.c): wave RAM can be accessed while CH3 plays, retriggering CH3 doesn't corrupt wave RAM, and powering off clears
// the length counters, which can't be written while powered off

namespace
{
	const unsigned char Wave[16] =
	{
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
	};

	class CgbApuHarness : public ApuHarness
	{
	public:
		explicit CgbApuHarness(bool powerOn = true) : ApuHarness(false)
		{
			Device.SetCgbMode(true);
			if (powerOn) PowerOn();
		}
	};

	void StartWave(ApuHarness& h)
	{
		for (int i = 0; i < 16; i++) h.Write(static_cast<unsigned short>(WaveRam + i), Wave[i]);
		h.Write(Nr30, 0x80);
		h.Write(Nr32, 0x20);
		h.Write(Nr33, 2000 & 0xff);
		h.Write(Nr34, 0x80 | (2000 >> 8));
	}
}

TEST(ApuCgbTests, WaveRamReadsTheByteCh3IsPlayingAtAnyTime)
{
	CgbApuHarness h;
	StartWave(h);

	// The DMG reads $FF except on the cycle CH3 reads; the CGB always sees the byte being played, whatever the address
	for (int i = 0; i < 300; i++)
	{
		h.Run(7);
		ASSERT_NE(0xff, h.Read(WaveRam + 5)) << "after " << h.Now << " cycles";
	}
}

TEST(ApuCgbTests, WaveRamWritesWhileCh3PlaysGoToTheByteBeingPlayed)
{
	CgbApuHarness h;
	StartWave(h);
	h.Run(150);
	auto playing = h.Read(WaveRam);
	h.Write(WaveRam + 9, 0x5a);
	EXPECT_EQ(0x5a, h.Read(WaveRam + 3));
	EXPECT_NE(0x5a, playing);
}

TEST(ApuCgbTests, RetriggeringCh3DoesntCorruptWaveRam)
{
	// Retrigger on every cycle across a few reads, any of which the DMG would corrupt wave RAM on
	for (int offset = 0; offset < 200; offset++)
	{
		CgbApuHarness h;
		StartWave(h);
		h.Run(offset);
		h.Write(Nr34, 0x80 | (2000 >> 8));
		h.Write(Nr30, 0x00);

		for (int i = 0; i < 16; i++) ASSERT_EQ(Wave[i], h.Read(static_cast<unsigned short>(WaveRam + i))) << "offset " << offset << ", byte " << i;
	}
}

class ApuCgbPowerLengthTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuCgbPowerLengthTests, PoweringOffClearsTheLengthCounter)
{
	// Length 1 loaded before powering off would end the channel at the first length clock; cleared, a trigger loads
	// the full length instead
	CgbApuHarness h;
	int channel = GetParam();
	h.SetLength(channel, 1);
	h.PowerOff();
	h.PowerOn();

	h.EnableDac(channel);
	h.Trigger(channel, true);
	h.RunTo(LengthClock(2));
	EXPECT_TRUE(h.Active(channel));
}

TEST_P(ApuCgbPowerLengthTests, LengthWritesWhilePoweredOffAreIgnored)
{
	CgbApuHarness h;
	int channel = GetParam();
	h.PowerOff();
	h.SetLength(channel, 1);
	h.PowerOn();

	h.EnableDac(channel);
	h.Trigger(channel, true);
	h.RunTo(LengthClock(2));
	EXPECT_TRUE(h.Active(channel));
}

INSTANTIATE_TEST_CASE_P(ApuChannels, ApuCgbPowerLengthTests, ::testing::Range(1, 5));

TEST(ApuCgbTests, PcmRegistersCombineTheChannelsOutputs)
{
	CgbApuHarness h;

	// CH1 on at volume 5 with duty 75% at the highest frequency, so it's high most of the time; CH2 silent
	h.Write(Nr11, 0xc0);
	h.Write(Nr12, 0x50);
	h.Write(Nr13, 0xff);
	h.Write(Nr14, 0x87);
	for (int i = 0; i < 64 && h.Out(1) != 5; i++) h.Run(1);
	ASSERT_EQ(5, h.Out(1));

	EXPECT_EQ(0x05, h.Device.ReadPcm(0xff76));
	EXPECT_EQ(0x00, h.Device.ReadPcm(0xff77));

	// CH2 the same at volume 9, in the high nibble
	h.Write(Nr21, 0xc0);
	h.Write(Nr22, 0x90);
	h.Write(Nr23, 0xff);
	h.Write(Nr24, 0x87);
	for (int i = 0; i < 64 && (h.Out(1) != 5 || h.Out(2) != 9); i++) h.Run(1);
	ASSERT_EQ(9, h.Out(2));
	EXPECT_EQ(0x95, h.Device.ReadPcm(0xff76));
}

TEST(ApuCgbTests, PcmRegistersReadZeroForChannelsWhoseDacIsOff)
{
	CgbApuHarness h;
	EXPECT_EQ(0, h.Device.ReadPcm(0xff76));
	EXPECT_EQ(0, h.Device.ReadPcm(0xff77));
}
