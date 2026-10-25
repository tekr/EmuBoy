#pragma once

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
#include "../core/Apu.h"

// Shared helpers for the ApuTests*.cpp files.
//
// Timing convention used by every test: ApuHarness resets the APU's divider and powers it on, so "Now" is the
// number of T-cycles since a divider of 0 and a freshly powered frame sequencer (next step = 0). Frame sequencer
// event number k (k = 1, 2, ...) then happens at T = 8192 * k and executes step (k - 1) % 8:
//   length clocks   at events 1, 3, 5, ...   (steps 0, 2, 4, 6)  -> clock n is at T = 8192 * (2n - 1)
//   sweep clocks    at events 3, 7, 11, ...  (steps 2, 6)         -> clock n is at T = 24576 + 32768 * (n - 1)
//   envelope clocks at events 8, 16, ...     (step 7)             -> clock n is at T = 65536 * n
// (gb-apu-spec.md sections 0 and 3.)

namespace ApuTest
{
	static const unsigned short Nr10 = 0xff10;
	static const unsigned short Nr11 = 0xff11;
	static const unsigned short Nr12 = 0xff12;
	static const unsigned short Nr13 = 0xff13;
	static const unsigned short Nr14 = 0xff14;
	static const unsigned short Nr21 = 0xff16;
	static const unsigned short Nr22 = 0xff17;
	static const unsigned short Nr23 = 0xff18;
	static const unsigned short Nr24 = 0xff19;
	static const unsigned short Nr30 = 0xff1a;
	static const unsigned short Nr31 = 0xff1b;
	static const unsigned short Nr32 = 0xff1c;
	static const unsigned short Nr33 = 0xff1d;
	static const unsigned short Nr34 = 0xff1e;
	static const unsigned short Nr41 = 0xff20;
	static const unsigned short Nr42 = 0xff21;
	static const unsigned short Nr43 = 0xff22;
	static const unsigned short Nr44 = 0xff23;
	static const unsigned short Nr50 = 0xff24;
	static const unsigned short Nr51 = 0xff25;
	static const unsigned short Nr52 = 0xff26;
	static const unsigned short WaveRam = 0xff30;

	static const long long FrameSequencerPeriod = 8192;

	inline long long FrameSequencerEvent(int k)
	{
		return FrameSequencerPeriod * k;
	}

	inline long long LengthClock(int n)
	{
		return FrameSequencerEvent(2 * n - 1);
	}

	inline long long SweepClock(int n)
	{
		return FrameSequencerEvent(4 * n - 1);
	}

	inline long long EnvelopeClock(int n)
	{
		return FrameSequencerEvent(8 * n);
	}

	// index 1-4 selects NRx1..NRx4 (for channel 3, NRx2 is NR32; its DAC switch is NR30)
	inline unsigned short ChannelRegister(int channel, int index)
	{
		static const unsigned short registers[4][4] =
		{
			{ 0xff11, 0xff12, 0xff13, 0xff14 },
			{ 0xff16, 0xff17, 0xff18, 0xff19 },
			{ 0xff1b, 0xff1c, 0xff1d, 0xff1e },
			{ 0xff20, 0xff21, 0xff22, 0xff23 }
		};

		return registers[channel - 1][index - 1];
	}

	inline int MaxLength(int channel)
	{
		return channel == 3 ? 256 : 64;
	}

	// Duty patterns, position 0 first (spec 4.1)
	inline int DutyBit(int duty, int position)
	{
		static const int patterns[4][8] =
		{
			{ 0, 0, 0, 0, 0, 0, 0, 1 },
			{ 1, 0, 0, 0, 0, 0, 0, 1 },
			{ 1, 0, 0, 0, 0, 1, 1, 1 },
			{ 0, 1, 1, 1, 1, 1, 1, 0 }
		};

		return patterns[duty][position & 7];
	}

	// The LFSR in the Pan Docs / SameBoy convention (spec 4.3): reset to 0, XNOR feedback into bit 14 (and bit 6 in
	// 7-bit mode), output is bit 0 after the step. The wiki's convention is the bitwise complement and gives the
	// same output.
	class LfsrModel
	{
	public:
		explicit LfsrModel(bool narrow) : _narrow(narrow)
		{
		}

		void Step()
		{
			unsigned feedback = (_state ^ (_state >> 1) ^ 1) & 1;
			_state >>= 1;
			_state = (_state & ~0x4000u) | (feedback << 14);

			if (_narrow)
			{
				_state = (_state & ~0x40u) | (feedback << 6);
			}
		}

		int Output() const
		{
			return (_state & 1) ? 15 : 0;
		}

	private:
		bool _narrow;
		unsigned _state = 0;
	};

	class ApuHarness
	{
	public:
		Apu Device{ 44100 };
		long long Now = 0;

		// Starts from a known state: powered off, divider reset, then optionally powered on (nothing else is assumed
		// about the state of a newly constructed Apu).
		explicit ApuHarness(bool powerOn = true)
		{
			Device.WriteRegister(Nr52, 0x00);
			Device.ResetDivider();

			if (powerOn)
			{
				Device.WriteRegister(Nr52, 0x80);
			}
		}

		void Write(unsigned short address, int value)
		{
			Device.WriteRegister(address, static_cast<unsigned char>(value));
		}

		int Read(unsigned short address)
		{
			return Device.ReadRegister(address);
		}

		void PowerOff()
		{
			Write(Nr52, 0x00);
		}

		void PowerOn()
		{
			Write(Nr52, 0x80);
		}

		void ResetDivider()
		{
			Device.ResetDivider();
			Now = 0;
		}

		void Run(long long cycles)
		{
			while (cycles > 0)
			{
				int chunk = static_cast<int>(std::min<long long>(cycles, 1 << 24));
				Device.RunCycles(chunk);
				cycles -= chunk;
				Now += chunk;
			}
		}

		void RunTo(long long target)
		{
			ASSERT_GE(target, Now);
			Run(target - Now);
		}

		bool Active(int channel)
		{
			return ((Read(Nr52) >> (channel - 1)) & 1) != 0;
		}

		// Runs to `time`, expecting the channel to be on until the cycle before and off from then
		void ExpectTurnsOffAt(int channel, long long time)
		{
			RunTo(time - 1);
			EXPECT_TRUE(Active(channel)) << "channel " << channel << " one cycle before " << time;
			RunTo(time);
			EXPECT_FALSE(Active(channel)) << "channel " << channel << " at " << time;
		}

		int Out(int channel) const
		{
			return Device.ChannelOutput(channel);
		}

		// Highest output seen when sampling every 4 T-cycles for `cycles` T-cycles. Used to read the volume of a
		// channel whose output also depends on duty/LFSR/wave position.
		int Peak(int channel, int cycles = 28)
		{
			int peak = -1;

			for (int i = 0; i < cycles / 4; i++)
			{
				Run(4);
				peak = std::max(peak, Out(channel));
			}

			return peak;
		}

		// T-cycle counts (Now) at which the output goes from zero to non-zero, stepping one cycle at a time
		std::vector<long long> RisingEdges(int channel, size_t count, long long limit)
		{
			std::vector<long long> edges;
			int previous = Out(channel);

			for (long long i = 0; i < limit && edges.size() < count; i++)
			{
				Run(1);
				int current = Out(channel);

				if (previous <= 0 && current > 0)
				{
					edges.push_back(Now);
				}

				previous = current;
			}

			return edges;
		}

		// T-cycle counts at which the output changes at all
		std::vector<long long> Changes(int channel, long long limit)
		{
			std::vector<long long> changes;
			int previous = Out(channel);

			for (long long i = 0; i < limit; i++)
			{
				Run(1);
				int current = Out(channel);

				if (current != previous)
				{
					changes.push_back(Now);
				}

				previous = current;
			}

			return changes;
		}

		std::vector<short> Take()
		{
			std::vector<short> samples;
			Device.TakeSamples(samples);
			return samples;
		}

		// ---- channel set-up helpers; all assume the APU is powered on ----

		// DAC on with a maximum, non-changing volume
		void EnableDac(int channel)
		{
			if (channel == 3)
			{
				Write(Nr30, 0x80);
				Write(Nr32, 0x20);
			}
			else
			{
				Write(ChannelRegister(channel, 2), 0xf0);
			}
		}

		void DisableDac(int channel)
		{
			if (channel == 3)
			{
				Write(Nr30, 0x00);
			}
			else
			{
				Write(ChannelRegister(channel, 2), 0x00);
			}
		}

		// Loads the length counter with `length` (1..MaxLength) via NRx1 (duty bits 0)
		void SetLength(int channel, int length)
		{
			Write(ChannelRegister(channel, 1), MaxLength(channel) - length);
		}

		// Writes NRx4 with trigger set (frequency high bits 0)
		void Trigger(int channel, bool lengthEnable)
		{
			Write(ChannelRegister(channel, 4), 0x80 | (lengthEnable ? 0x40 : 0));
		}

		void SetLengthEnable(int channel, bool lengthEnable)
		{
			Write(ChannelRegister(channel, 4), lengthEnable ? 0x40 : 0);
		}

		// Square channel 1 or 2 (channel 1's sweep is whatever NR10 holds)
		void StartSquare(int channel, int duty, int envelope, int frequency, bool lengthEnable = false)
		{
			Write(ChannelRegister(channel, 1), duty << 6);
			Write(ChannelRegister(channel, 2), envelope);
			Write(ChannelRegister(channel, 3), frequency & 0xff);
			Write(ChannelRegister(channel, 4), 0x80 | (lengthEnable ? 0x40 : 0) | ((frequency >> 8) & 7));
		}

		void LoadWave(const unsigned char (&bytes)[16])
		{
			for (int i = 0; i < 16; i++)
			{
				Write(static_cast<unsigned short>(WaveRam + i), bytes[i]);
			}
		}

		// volumeCode is NR32 bits 6-5
		void StartWave(int volumeCode, int frequency, bool lengthEnable = false)
		{
			Write(Nr30, 0x80);
			Write(Nr32, volumeCode << 5);
			Write(Nr33, frequency & 0xff);
			Write(Nr34, 0x80 | (lengthEnable ? 0x40 : 0) | ((frequency >> 8) & 7));
		}

		void StartNoise(int shift, bool narrow, int divisorCode, int envelope = 0xf0, bool lengthEnable = false)
		{
			Write(Nr42, envelope);
			Write(Nr43, (shift << 4) | (narrow ? 0x08 : 0) | divisorCode);
			Write(Nr44, 0x80 | (lengthEnable ? 0x40 : 0));
		}

		// Channel 1, 2 or 4 at its fastest rate, so that Volume() can see the envelope volume in a short window
		void StartEnvelopeChannel(int channel, int envelope)
		{
			if (channel == 4)
			{
				StartNoise(0, false, 0, envelope);
			}
			else
			{
				StartSquare(channel, 2, envelope, 2047);
			}
		}

		// The squares reach a duty-high step within 28 cycles; the noise LFSR needs a longer window to output a 1
		int Volume(int channel)
		{
			return Peak(channel, channel == 4 ? 2000 : 28);
		}

		// Volume shortly before / after an envelope clock; "before" leaves room for the 2000-cycle noise window
		int VolumeBefore(int channel, long long clock)
		{
			RunTo(clock - 2100);
			return Volume(channel);
		}

		int VolumeAfter(int channel, long long clock)
		{
			RunTo(clock + 100);
			return Volume(channel);
		}
	};
}
