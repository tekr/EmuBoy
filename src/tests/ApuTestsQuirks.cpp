#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstring>
#include "ApuTestHelpers.h"

using namespace ApuTest;

// Quirk tests. Expectations come from gb-apu-spec.md (sections named per group), the gbdev wiki "Obscure Behavior"
// and SameBoy's Core/apu.c (nrx2_glitch, GB_apu_init, GB_apu_read/write, the NR34 trigger case). Where DMG
// behaviour is documented as unpredictable or the sources disagree, the case is not tested.

namespace
{
	// 32 samples, consecutive samples always differ (so ChannelOutput changes on every wave RAM read) and all 16
	// bytes are distinct (so corruption/redirection is visible).
	const unsigned char QuirkWave[16] =
	{
		0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf1,
		0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x1f
	};

	void StartEnvelopeChannel(ApuHarness& h, int channel, int envelope)
	{
		if (channel == 4)
		{
			h.StartNoise(0, false, 0, envelope);
		}
		else
		{
			h.StartSquare(channel, 2, envelope, 2047);
		}
	}

	int WindowFor(int channel)
	{
		return channel == 4 ? 2000 : 28;
	}
}

// ---- Zombie-mode NRx2 writes (spec 10.2) ----
//
// Only the case that the wiki, the spec ("consistent on all models") and SameBoy's DMG model agree on is tested:
// the envelope is in increase mode with period 0 ($x8) and $x8 is written again while the channel plays. Each such
// write adds 1 to the live volume (mod 16); the volume nibble of the new value is ignored. SameBoy's DMG model
// (old -> $FF -> new) gives +1 for these writes too. Other combinations ($x0 and $x7 writes) are documented as
// unpredictable on DMG and are not tested.

class ApuZombieTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuZombieTests, WritingEightRepeatedlyAddsOneToTheVolumeEachTime)
{
	ApuHarness h;
	int channel = GetParam();
	StartEnvelopeChannel(h, channel, 0x58); // volume 5, increase, period 0
	h.RunTo(3000);
	ASSERT_EQ(5, h.Peak(channel, WindowFor(channel)));

	for (int writes = 1; writes <= 18; writes++)
	{
		h.Write(ChannelRegister(channel, 2), 0x08);
		ASSERT_EQ((5 + writes) & 15, h.Peak(channel, WindowFor(channel))) << "after " << writes << " writes";
	}
}

TEST_P(ApuZombieTests, VolumeNibbleOfTheWrittenValueIsIgnored)
{
	ApuHarness h;
	int channel = GetParam();
	StartEnvelopeChannel(h, channel, 0x98); // volume 9, increase, period 0
	h.RunTo(3000);

	h.Write(ChannelRegister(channel, 2), 0x28);

	EXPECT_EQ(10, h.Peak(channel, WindowFor(channel)));
}

TEST_P(ApuZombieTests, WrittenNrx2StillReadsBackAsWritten)
{
	ApuHarness h;
	int channel = GetParam();
	StartEnvelopeChannel(h, channel, 0x58);
	h.Write(ChannelRegister(channel, 2), 0x08);

	EXPECT_EQ(0x08, h.Read(ChannelRegister(channel, 2)));
}

INSTANTIATE_TEST_CASE_P(ApuQuirks, ApuZombieTests, ::testing::Values(1, 2, 4));

// ---- Envelope timer +1 when triggered right before the envelope step (spec 4.4, 5) ----
//
// "If a channel is triggered when the next frame sequencer step will clock the envelope, the envelope timer is
// reloaded with one more than normal" (wiki Obscure Behavior; Pan Docs). SameBoy's countdown model gives the same
// observable result: the first volume change comes one envelope period later than for any other trigger time.
// Here the trigger is at 57444, just after event 7 (step 6), so the next step is 7.

class ApuEnvelopeDelayTests : public ::testing::TestWithParam<int>
{
protected:
	int VolumeAt(ApuHarness& h, int channel, long long time)
	{
		h.RunTo(time);

		return h.Peak(channel, WindowFor(channel));
	}
};

TEST_P(ApuEnvelopeDelayTests, PeriodOneFirstChangeComesAtTheSecondEnvelopeClock)
{
	ApuHarness h;
	int channel = GetParam();
	h.RunTo(7 * 8192 + 100);
	StartEnvelopeChannel(h, channel, 0xf1); // volume 15, decrease, period 1

	EXPECT_EQ(15, VolumeAt(h, channel, 65536 + 100));      // a normal trigger would already be 14 here
	EXPECT_EQ(15, VolumeAt(h, channel, 2 * 65536 - 2100));
	EXPECT_EQ(14, VolumeAt(h, channel, 2 * 65536 + 100));
}

TEST_P(ApuEnvelopeDelayTests, PeriodTwoFirstChangeComesAtTheThirdEnvelopeClock)
{
	ApuHarness h;
	int channel = GetParam();
	h.RunTo(7 * 8192 + 100);
	StartEnvelopeChannel(h, channel, 0xf2); // volume 15, decrease, period 2

	EXPECT_EQ(15, VolumeAt(h, channel, 2 * 65536 + 100));  // a normal trigger would already be 14 here
	EXPECT_EQ(15, VolumeAt(h, channel, 3 * 65536 - 2100));
	EXPECT_EQ(14, VolumeAt(h, channel, 3 * 65536 + 100));
}

TEST_P(ApuEnvelopeDelayTests, TriggerWhenTheNextStepIsNotSevenHasNoDelay)
{
	// control: trigger at 49252 (after event 6, next step is 6)
	ApuHarness h;
	int channel = GetParam();
	h.RunTo(6 * 8192 + 100);
	StartEnvelopeChannel(h, channel, 0xf1);

	EXPECT_EQ(15, VolumeAt(h, channel, 65536 - 2100));
	EXPECT_EQ(14, VolumeAt(h, channel, 65536 + 100));
}

INSTANTIATE_TEST_CASE_P(ApuQuirks, ApuEnvelopeDelayTests, ::testing::Values(1, 2, 4));

// ---- Power-on with DIV bit 4 (internal bit 12) set (spec 2.2, SameBoy GB_apu_init) ----
//
// SameBoy: "When turning the APU on while DIV's bit 4 is on, the first DIV/APU event is skipped." The events are
// then: skipped, step 0, step 1, step 2 ... instead of step 0, 1, 2. Observed through the sweep clock (steps 2 and
// 6), which has no extra-clock side effects of its own: with f = 1000, shift 1, pace 1 the first sweep clock
// overflows and disables CH1.
// (Observing it through length would mix in the extra-length-clock rule, whose parity SameBoy derives from its
// own counter, so it is not used here.)

TEST(ApuPowerOnGlitchTests, PowerOnWithBit12ClearUsesTheFirstEventAsStepZero)
{
	ApuHarness h(false);
	h.Run(3000); // bit 12 clear
	h.PowerOn();
	h.Write(Nr10, 0x11);
	h.StartSquare(1, 0, 0xf0, 1000);

	// events at 8192 (step 0), 16384 (1), 24576 (2: first sweep clock)
	h.ExpectTurnsOffAt(1, 24576);
}

TEST(ApuPowerOnGlitchTests, PowerOnWithBit12SetSkipsTheFirstEvent)
{
	ApuHarness h(false);
	h.Run(6000); // 0x1770: bit 12 set
	h.PowerOn();
	h.Write(Nr10, 0x11);
	h.StartSquare(1, 0, 0xf0, 1000);

	// events at 8192 (skipped), 16384 (step 0), 24576 (1), 32768 (2: first sweep clock)
	h.RunTo(24576 + 1000);
	EXPECT_TRUE(h.Active(1));
	h.ExpectTurnsOffAt(1, 32768);
}

// ---- Wave RAM access while CH3 plays (spec 10.1, DMG) ----
//
// DMG: while CH3 is active, a CPU access to FF30-FF3F only works on the cycle where CH3 itself reads wave RAM;
// then it reaches the byte being read (position / 2) whatever the address; otherwise reads return $FF and writes
// are ignored (Pan Docs "Wave pattern RAM"; wiki; SameBoy wave_form_just_read).
//
// Contract used here: the APU is advanced by RunCycles up to the cycle of the access and the access happens right
// after. In the emulator that is once per CPU instruction (and at the access's M-cycle if the implementation uses
// access timing). These tests step one T-cycle at a time and use the cycle where ChannelOutput(3) changes (the
// sample buffer was just refilled) as "the cycle CH3 reads". Every sample differs from the previous one, so the
// output changes on every read. After k reads the position is k % 32 (the first read after a trigger is position 1)
// and the byte being read is QuirkWave[(k % 32) / 2]. This is the single exact cycle SameBoy allows; the spec
// marks the exact window as uncertain ([PD-R] "same cycle", [WIKI] "a couple of clocks"), so only the exact cycle
// and cycles well away from it are checked.

namespace
{
	const int WaveAccessFrequency = 2000; // period (2048 - 2000) * 2 = 96 T-cycles

	// Steps one cycle at a time until the output changes; false if that does not happen within a few periods
	bool AdvanceToNextWaveRead(ApuHarness& h)
	{
		int previous = h.Out(3);

		for (int i = 0; i < 400; i++)
		{
			h.Run(1);
			int current = h.Out(3);

			if (current != previous)
			{
				return true;
			}

			previous = current;
		}

		return false;
	}

	void StartAccessTestWave(ApuHarness& h)
	{
		h.LoadWave(QuirkWave);
		h.StartWave(1, WaveAccessFrequency);
	}
}

TEST(ApuWaveAccessTests, ReadOnTheCycleCh3ReadsReturnsTheByteBeingRead)
{
	ApuHarness h;
	StartAccessTestWave(h);

	for (int k = 1; k <= 8; k++)
	{
		ASSERT_TRUE(AdvanceToNextWaveRead(h)) << "read " << k;

		// any address in the range reaches the byte being read
		EXPECT_EQ(QuirkWave[(k % 32) / 2], h.Read(static_cast<unsigned short>(WaveRam + (k * 5) % 16))) << "read " << k;
	}
}

TEST(ApuWaveAccessTests, ReadOnAnyOtherCycleReturnsFf)
{
	ApuHarness h;
	StartAccessTestWave(h);
	ASSERT_TRUE(AdvanceToNextWaveRead(h));

	h.Run(1);
	EXPECT_EQ(0xff, h.Read(WaveRam));
	h.Run(40);
	EXPECT_EQ(0xff, h.Read(WaveRam + 3));
	h.Run(30);
	EXPECT_EQ(0xff, h.Read(WaveRam + 15));
}

TEST(ApuWaveAccessTests, WriteOnTheCycleCh3ReadsGoesToTheByteBeingReadWhateverTheAddress)
{
	ApuHarness h;
	StartAccessTestWave(h);
	ASSERT_TRUE(AdvanceToNextWaveRead(h)); // position 1 -> byte 0
	h.Write(WaveRam + 12, 0xab);
	ASSERT_TRUE(AdvanceToNextWaveRead(h)); // position 2 -> byte 1
	ASSERT_TRUE(AdvanceToNextWaveRead(h)); // position 3 -> byte 1
	ASSERT_TRUE(AdvanceToNextWaveRead(h)); // position 4 -> byte 2
	h.Write(WaveRam + 1, 0xcd);

	h.Write(Nr30, 0x00); // stop CH3 so wave RAM can be inspected normally

	EXPECT_EQ(0xab, h.Read(WaveRam + 0));
	EXPECT_EQ(QuirkWave[1], h.Read(WaveRam + 1));
	EXPECT_EQ(0xcd, h.Read(WaveRam + 2));
	EXPECT_EQ(QuirkWave[12], h.Read(WaveRam + 12));
}

TEST(ApuWaveAccessTests, WriteOnAnyOtherCycleIsIgnored)
{
	ApuHarness h;
	StartAccessTestWave(h);
	ASSERT_TRUE(AdvanceToNextWaveRead(h));
	h.Run(1);
	h.Write(WaveRam + 0, 0xee);
	h.Run(30);
	h.Write(WaveRam + 7, 0xee);
	h.Run(30);
	h.Write(WaveRam + 15, 0xee);

	h.Write(Nr30, 0x00);

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ(QuirkWave[i], h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i;
	}
}

TEST(ApuWaveAccessTests, WaveRamIsNormallyAccessibleAfterCh3IsStopped)
{
	ApuHarness h;
	StartAccessTestWave(h);
	h.Run(1000);
	h.Write(Nr30, 0x00);

	h.Write(WaveRam + 9, 0x5a);

	EXPECT_EQ(0x5a, h.Read(WaveRam + 9));
}

TEST(ApuWaveAccessTests, WaveRamIsNormallyAccessibleWhenCh3IsOnlyDacEnabledAndNotTriggered)
{
	ApuHarness h;
	h.Write(Nr30, 0x80);

	h.Write(WaveRam + 4, 0x77);

	EXPECT_EQ(0x77, h.Read(WaveRam + 4));
}

// ---- Wave RAM corruption when CH3 is retriggered while playing (spec 10.4, DMG-B) ----
//
// Retriggering one cycle before CH3's read corrupts wave RAM (wiki; Pan Docs "RETRIGGERING CAUTION"; SameBoy NR34
// case, which blargg's 10-wave trigger while on expects). With pos the position before that read,
// offset = ((pos + 1) >> 1) & 0xF: if offset < 4 only byte 0 is overwritten with byte[offset], otherwise bytes 0-3
// are overwritten with the aligned block byte[offset & ~3 ...+3]. This varies between units (see the spec), so only
// the common DMG-B pattern is tested. "j-th read" below means the read that moves the position to j, so pos = j - 1
// and offset = (j >> 1) & 0xF.

namespace
{
	std::vector<long long> WaveReadTimes(int count)
	{
		ApuHarness h;
		StartAccessTestWave(h);
		std::vector<long long> times;

		while (static_cast<int>(times.size()) < count && AdvanceToNextWaveRead(h))
		{
			times.push_back(h.Now);
		}

		return times;
	}
}

class ApuWaveRetriggerTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuWaveRetriggerTests, RetriggerOneCycleBeforeAReadCorruptsWaveRam)
{
	int j = GetParam();
	auto times = WaveReadTimes(j);
	ASSERT_EQ(static_cast<size_t>(j), times.size());

	ApuHarness h;
	StartAccessTestWave(h);
	h.RunTo(times[j - 1] - 1);
	h.Write(Nr34, 0x80 | (WaveAccessFrequency >> 8));
	h.Write(Nr30, 0x00);

	unsigned char expected[16];
	std::memcpy(expected, QuirkWave, 16);
	int offset = (j >> 1) & 0xf;

	if (offset < 4)
	{
		expected[0] = QuirkWave[offset];
	}
	else
	{
		std::memcpy(expected, QuirkWave + (offset & ~3), 4);
	}

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ(expected[i], h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i << " read " << j;
	}
}

// offsets 1, 4, 9 and 14: one byte copied, and block copies from bytes 4-7, 8-11 and 12-15
INSTANTIATE_TEST_CASE_P(ApuQuirks, ApuWaveRetriggerTests, ::testing::Values(2, 9, 18, 29));

TEST(ApuWaveRetriggerTests, RetriggerAfterStoppingTheChannelDoesNotCorruptWaveRam)
{
	// the "avoidance" from the wiki: NR30 = 0 then $80 before retriggering
	auto times = WaveReadTimes(9);
	ASSERT_EQ(9u, times.size());

	ApuHarness h;
	StartAccessTestWave(h);
	h.RunTo(times[8] - 1);
	h.Write(Nr30, 0x00);
	h.Write(Nr30, 0x80);
	h.Write(Nr34, 0x80 | (WaveAccessFrequency >> 8));
	h.Write(Nr30, 0x00);

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ(QuirkWave[i], h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i;
	}
}

TEST(ApuWaveRetriggerTests, RetriggerAwayFromAReadDoesNotCorruptWaveRam)
{
	auto times = WaveReadTimes(9);
	ASSERT_EQ(9u, times.size());

	ApuHarness h;
	StartAccessTestWave(h);
	h.RunTo(times[8] - 40);
	h.Write(Nr34, 0x80 | (WaveAccessFrequency >> 8));
	h.Write(Nr30, 0x00);

	for (int i = 0; i < 16; i++)
	{
		EXPECT_EQ(QuirkWave[i], h.Read(static_cast<unsigned short>(WaveRam + i))) << "byte " << i;
	}
}

// ---- Noise clock shift 14/15 (spec 4.3): the LFSR receives no clocks ----
//
// Complements ApuNoiseTests.ClockShiftOf14/15ReceivesNoClocks (which start with that shift): here the shift is
// changed while the channel is already running with a fast, changing LFSR, and the output must freeze.

namespace
{
	// Samples ChannelOutput(4) every 8 cycles for `count` samples; true if any differs from the first
	bool OutputChanges(ApuHarness& h, int count)
	{
		int first = h.Out(4);

		for (int i = 0; i < count; i++)
		{
			h.Run(8);

			if (h.Out(4) != first)
			{
				return true;
			}
		}

		return false;
	}
}

TEST(ApuNoiseLockUpTests, FastLfsrKeepsChangingWithShiftZero)
{
	// control: the same scenario without the shift write
	ApuHarness h;
	h.StartNoise(0, false, 0);
	h.Run(8 * 400);

	EXPECT_TRUE(OutputChanges(h, 2000));
}

class ApuNoiseFreezeTests : public ::testing::TestWithParam<int>
{
};

TEST_P(ApuNoiseFreezeTests, ChangingTheShiftTo14Or15StopsTheLfsr)
{
	ApuHarness h;
	h.StartNoise(0, false, 0);
	h.Run(8 * 400);

	h.Write(Nr43, GetParam() << 4);
	h.Run(64); // let any clock that was already due happen

	EXPECT_FALSE(OutputChanges(h, 2000));
}

TEST_P(ApuNoiseFreezeTests, TheChannelStaysEnabledWhileFrozen)
{
	ApuHarness h;
	h.StartNoise(0, false, 0);
	h.Write(Nr43, GetParam() << 4);
	h.Run(100000);

	EXPECT_TRUE(h.Active(4));
}

INSTANTIATE_TEST_CASE_P(ApuQuirks, ApuNoiseFreezeTests, ::testing::Values(14, 15));
