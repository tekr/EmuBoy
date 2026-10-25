#include "stdafx.h"
#include <gtest/gtest.h>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Spec references are to gb-apu-spec.md (sections "1.2", "2.1", ...).

// ---- Register read-back masks (spec 1.2) ----

namespace
{
	// OR mask for 0xff10-0xff25 and the unused 0xff27-0xff2f (0xff26 is handled separately)
	int ExpectedMask(unsigned short address)
	{
		static const int masks[] =
		{
			0x80, 0x3f, 0x00, 0xff, 0xbf, // NR10-NR14
			0xff, 0x3f, 0x00, 0xff, 0xbf, // unused, NR21-NR24
			0x7f, 0xff, 0x9f, 0xff, 0xbf, // NR30-NR34
			0xff, 0xff, 0x00, 0x00, 0xbf, // unused, NR41-NR44
			0x00, 0x00                    // NR50, NR51
		};

		if (address >= 0xff10 && address <= 0xff25)
		{
			return masks[address - 0xff10];
		}

		return 0xff;
	}
}

class ApuRegisterMaskTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuRegisterMaskTests, ReadsBackEveryWrittenValueOrMaskWhenPoweredOn)
{
	ApuHarness h;
	auto address = static_cast<unsigned short>(GetParam());

	for (int value = 0; value < 256; value++)
	{
		h.Write(address, value);
		ASSERT_EQ(value | ExpectedMask(address), h.Read(address)) << "value written " << value;
	}
}

TEST_P(ApuRegisterMaskTests, ReadsOnlyTheMaskAfterPowerOff)
{
	ApuHarness h;
	auto address = static_cast<unsigned short>(GetParam());
	h.Write(address, 0xff);
	h.PowerOff();

	EXPECT_EQ(ExpectedMask(address), h.Read(address));
}

TEST_P(ApuRegisterMaskTests, IgnoresWritesWhilePoweredOff)
{
	// e.g. NR11 = $FF while off still reads $3F (blargg 01-registers #6)
	ApuHarness h(false);
	auto address = static_cast<unsigned short>(GetParam());
	int values[] = { 0xff, 0x55, 0xaa, 0x00 };

	for (int value : values)
	{
		h.Write(address, value);
		ASSERT_EQ(ExpectedMask(address), h.Read(address)) << "value written " << value;
	}
}

INSTANTIATE_TEST_CASE_P(ApuRegisters, ApuRegisterMaskTests, ::testing::Range(0xff10, 0xff26));

class ApuUnusedRegisterTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuUnusedRegisterTests, ReadsFfAndIgnoresWritesWhenOnOrOff)
{
	ApuHarness h;
	auto address = static_cast<unsigned short>(GetParam());

	h.Write(address, 0x00);
	EXPECT_EQ(0xff, h.Read(address));

	h.PowerOff();
	h.Write(address, 0x00);
	EXPECT_EQ(0xff, h.Read(address));
}

// 0xff15 and 0xff1f are in the previous range; this covers 0xff27-0xff2f
INSTANTIATE_TEST_CASE_P(ApuRegisters, ApuUnusedRegisterTests, ::testing::Range(0xff27, 0xff30));

TEST(ApuRegisterTests, WaveRamReadsBackWhatWasWrittenWhenPoweredOn)
{
	ApuHarness h;

	for (int i = 0; i < 16; i++)
	{
		h.Write(static_cast<unsigned short>(WaveRam + i), 0x10 * i + 0x0f - i);
	}

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ((0x10 * i + 0x0f - i) & 0xff, h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i;
	}
}

// ---- NR52 (spec 2.3) ----

TEST(ApuNr52Tests, ReadsF0WhenPoweredOnWithNoActiveChannels)
{
	ApuHarness h;

	EXPECT_EQ(0xf0, h.Read(Nr52));
}

TEST(ApuNr52Tests, Reads70WhenPoweredOff)
{
	ApuHarness h(false);

	EXPECT_EQ(0x70, h.Read(Nr52));
}

TEST(ApuNr52Tests, StatusBitsAreReadOnly)
{
	ApuHarness h;
	h.Write(Nr52, 0x8f);

	EXPECT_EQ(0xf0, h.Read(Nr52));
}

TEST(ApuNr52Tests, UnusedBitsReadAsOneWhenWrittenZeroAndWhenOne)
{
	ApuHarness h;

	h.Write(Nr52, 0xff);
	EXPECT_EQ(0xf0, h.Read(Nr52));

	h.Write(Nr52, 0x00);
	EXPECT_EQ(0x70, h.Read(Nr52));
}

class ApuChannelTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuChannelTests, TriggerSetsOnlyThatChannelsStatusBit)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Trigger(channel, false);

	EXPECT_EQ(0xf0 | (1 << (channel - 1)), h.Read(Nr52));
}

TEST_P(ApuChannelTests, WritingStatusBitsDoesNotEnableOrDisableTheChannel)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Trigger(channel, false);

	h.Write(Nr52, 0x80);
	EXPECT_TRUE(h.Active(channel));

	h.Write(Nr52, 0x8f ^ (1 << (channel - 1)));
	EXPECT_TRUE(h.Active(channel));
}

TEST_P(ApuChannelTests, WritingNr52WithPowerStillOnKeepsRegistersAndChannels)
{
	ApuHarness h;
	int channel = GetParam();
	h.EnableDac(channel);
	h.Trigger(channel, false);
	h.Write(Nr50, 0x5d);

	h.Write(Nr52, 0x80);

	EXPECT_TRUE(h.Active(channel));
	EXPECT_EQ(0x5d, h.Read(Nr50));
}

INSTANTIATE_TEST_CASE_P(ApuChannels, ApuChannelTests, ::testing::Range(1, 5));

// ---- Power off / on (spec 2.1, 2.2, 10.3) ----

TEST(ApuPowerTests, PowerOffClearsAllRegistersNr10ToNr51)
{
	ApuHarness h;

	for (unsigned short address = Nr10; address <= Nr51; address++)
	{
		h.Write(address, 0xff);
	}

	h.PowerOff();

	for (unsigned short address = Nr10; address <= Nr51; address++)
	{
		EXPECT_EQ(ExpectedMask(address), h.Read(address)) << "address " << address;
	}
}

TEST(ApuPowerTests, RegistersStayClearedAfterPoweringBackOn)
{
	ApuHarness h;
	h.Write(Nr50, 0x77);
	h.Write(Nr51, 0xff);
	h.Write(Nr12, 0xf3);
	h.Write(Nr43, 0x55);

	h.PowerOff();
	h.PowerOn();

	EXPECT_EQ(0x00, h.Read(Nr50));
	EXPECT_EQ(0x00, h.Read(Nr51));
	EXPECT_EQ(0x00, h.Read(Nr12));
	EXPECT_EQ(0x00, h.Read(Nr43));
}

TEST(ApuPowerTests, PowerOffDisablesEveryChannel)
{
	ApuHarness h;

	for (int channel = 1; channel <= 4; channel++)
	{
		h.EnableDac(channel);
		h.Trigger(channel, false);
	}

	ASSERT_EQ(0xff, h.Read(Nr52));

	h.PowerOff();
	EXPECT_EQ(0x70, h.Read(Nr52));

	h.PowerOn();
	EXPECT_EQ(0xf0, h.Read(Nr52));
}

TEST(ApuPowerTests, AllDacsAreOffAfterAPowerCycle)
{
	ApuHarness h;

	for (int channel = 1; channel <= 4; channel++)
	{
		h.EnableDac(channel);
		h.Trigger(channel, false);
	}

	h.PowerOff();
	h.PowerOn();

	for (int channel = 1; channel <= 4; channel++)
	{
		EXPECT_EQ(-1, h.Out(channel)) << "channel " << channel;
	}
}

TEST(ApuPowerTests, TriggerWrittenWhilePoweredOffDoesNothing)
{
	ApuHarness h(false);
	h.Write(Nr12, 0xf0);
	h.Write(Nr14, 0x80);
	h.PowerOn();

	EXPECT_EQ(0xf0, h.Read(Nr52));
}

TEST(ApuPowerTests, TriggerAfterPowerCycleIsIgnoredBecauseNr12WasCleared)
{
	// blargg 11-regs after power #2: the cleared NR12 turns the DAC off
	ApuHarness h;
	h.Write(Nr12, 0xf0);
	h.PowerOff();
	h.PowerOn();
	h.Write(Nr14, 0x80);

	EXPECT_FALSE(h.Active(1));
}

TEST(ApuPowerTests, WaveRamIsReadableAndWritableWhilePoweredOff)
{
	ApuHarness h(false);

	for (int i = 0; i < 16; i++)
	{
		h.Write(static_cast<unsigned short>(WaveRam + i), 0xa0 + i);
	}

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ(0xa0 + i, h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i;
	}
}

TEST(ApuPowerTests, WaveRamSurvivesAPowerCycle)
{
	ApuHarness h;

	for (int i = 0; i < 16; i++)
	{
		h.Write(static_cast<unsigned short>(WaveRam + i), 0x3c ^ (i * 17));
	}

	h.PowerOff();
	h.PowerOn();

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ((0x3c ^ (i * 17)) & 0xff, h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i;
	}
}

// Length counters are unaffected by power and writable while off on DMG (spec 2.1, 10.3; blargg 08, 11 #4).

class ApuPowerLengthTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuPowerLengthTests, LengthWrittenWhilePoweredOffIsUsedAfterPowerOn)
{
	ApuHarness h;
	int channel = GetParam();
	h.PowerOff();
	h.SetLength(channel, 1);
	h.PowerOn();

	h.EnableDac(channel);
	h.Trigger(channel, true);
	ASSERT_TRUE(h.Active(channel));

	h.ExpectTurnsOffAt(channel, LengthClock(1));
}

TEST_P(ApuPowerLengthTests, LengthLoadedBeforePowerOffSurvivesAPowerCycle)
{
	ApuHarness h;
	int channel = GetParam();
	h.SetLength(channel, 1);
	h.PowerOff();
	h.PowerOn();

	h.EnableDac(channel);
	h.Trigger(channel, true);

	h.ExpectTurnsOffAt(channel, LengthClock(1));
}

TEST_P(ApuPowerLengthTests, LengthCountersAreNotClockedWhilePoweredOff)
{
	// Counter is 2, length clock 1 takes it to 1. Power is off for a long time (that would expire it if it
	// were clocked), then back on at a divider phase of 1696 so the next frame sequencer event is step 0.
	ApuHarness h;
	int channel = GetParam();
	h.StartWithLength(channel, 2, true);
	h.RunTo(LengthClock(1));
	ASSERT_TRUE(h.Active(channel));

	h.PowerOff();
	h.RunTo(108192);
	h.PowerOn();

	h.EnableDac(channel);
	h.Trigger(channel, true);
	ASSERT_TRUE(h.Active(channel));

	// Next event is at the next multiple of 8192 after 108192 (= 13 * 8192 + 1696)
	long long nextEvent = FrameSequencerEvent(14);
	h.ExpectTurnsOffAt(channel, nextEvent);
}

INSTANTIATE_TEST_CASE_P(ApuChannels, ApuPowerLengthTests, ::testing::Range(1, 5));

// ---- Frame sequencer and the divider (spec 2.2, 3.1, 3.2) ----

TEST(ApuDividerTests, FirstFrameSequencerEventIsOneFullPeriodAfterADividerReset)
{
	ApuHarness h;
	h.StartWithLength(1, 1, true);

	h.ExpectTurnsOffAt(1, FrameSequencerEvent(1));
}

TEST(ApuDividerTests, ResettingTheDividerWithBit12ClearRestartsThePeriod)
{
	ApuHarness h;
	h.StartWithLength(1, 1, true);
	h.RunTo(2000); // divider is 2000: bit 12 is clear, so no falling edge
	h.ResetDivider();

	ASSERT_TRUE(h.Active(1));
	h.ExpectTurnsOffAt(1, FrameSequencerEvent(1));
}

TEST(ApuDividerTests, ResettingTheDividerWithBit12SetClocksTheFrameSequencerImmediately)
{
	// spec 3.1: a reset while the bit is set is a falling edge and gives an extra tick (step 0 = length clock)
	ApuHarness h;
	h.StartWithLength(1, 1, true);
	h.RunTo(6000); // 6000 = 0x1770: bit 12 set
	ASSERT_TRUE(h.Active(1));

	h.ResetDivider();

	EXPECT_FALSE(h.Active(1));
}

TEST(ApuDividerTests, ExtraTickFromAResetAdvancesTheStepCounterAndTheNextTickIsAFullPeriodLater)
{
	// Counter 2. Reset at 6000 gives the step 0 clock (-> 1). Then step 1 is 8192 later (no length clock) and
	// step 2 at 6000 + 2 * 8192 clocks it to 0.
	ApuHarness h;
	h.StartWithLength(1, 2, true);
	h.RunTo(6000);
	h.ResetDivider();
	ASSERT_TRUE(h.Active(1));

	// Now restarted at 0 with the divider, so step 1 is event 1 and step 2 is event 2
	h.ExpectTurnsOffAt(1, FrameSequencerEvent(2));
}

TEST(ApuDividerTests, PoweringOnDoesNotRestartTheFreeRunningDivider)
{
	// blargg 07 #5: time to the next event after power-on is just the free-running 8192-cycle phase
	ApuHarness h(false);
	h.Run(3000);
	h.PowerOn();
	h.StartWithLength(1, 1, true);

	h.ExpectTurnsOffAt(1, FrameSequencerEvent(1));
}

TEST(ApuDividerTests, PoweringOnResetsTheStepCounterToZero)
{
	// 24676 is just after step 2 (event 3), so the next step would be 3 (no length clock). After a power cycle the
	// next step is 0 again, so the very next event (event 4) clocks length.
	ApuHarness h;
	h.RunTo(24676);
	h.PowerOff();
	h.PowerOn();
	h.StartWithLength(1, 1, true);

	h.ExpectTurnsOffAt(1, FrameSequencerEvent(4));
}
