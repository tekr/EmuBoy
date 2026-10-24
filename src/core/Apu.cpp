#include "stdafx.h"
#include "Apu.h"
#include <algorithm>
#include <cmath>

const unsigned char Apu::ReadMasks[RegisterCount] =
{
	0x80, 0x3f, 0x00, 0xff, 0xbf,	// NR10-NR14
	0xff, 0x3f, 0x00, 0xff, 0xbf,	// unused, NR21-NR24
	0x7f, 0xff, 0x9f, 0xff, 0xbf,	// NR30-NR34
	0xff, 0xff, 0x00, 0x00, 0xbf,	// unused, NR41-NR44
	0x00, 0x00, 0x70,				// NR50-NR52
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0	// wave RAM
};

// Output bit per duty position 0-7 (gbdev wiki "Square Wave")
const unsigned char Apu::DutyPatterns[4] = { 0x80, 0x81, 0xe1, 0x7e };

// DMG wave RAM powers up "somewhat random"; this is the gbdev wiki's example
const unsigned char Apu::InitialWaveRam[16] =
{
	0x84, 0x40, 0x43, 0xaa, 0x2d, 0x78, 0x92, 0x3c, 0x60, 0x59, 0x59, 0xb0, 0x34, 0xb8, 0x2e, 0xda
};

void Envelope::Trigger(unsigned char nrx2, bool nextStepClocksEnvelope)
{
	Volume = nrx2 >> 4;
	Increase = (nrx2 & 8) != 0;
	Period = nrx2 & 7;
	Running = true;

	// A period of 0 counts as 8, and a trigger just before an envelope step delays it by one (Pan Docs, Audio Details)
	Timer = (Period ? Period : 8) + (nextStepClocksEnvelope ? 1 : 0);
}

void Envelope::Clock()
{
	if (--Timer > 0) return;

	Timer = Period ? Period : 8;
	if (!Period || !Running) return;

	auto volume = Volume + (Increase ? 1 : -1);
	if (volume >= 0 && volume <= 15) Volume = volume;
	else Running = false;
}

// "Zombie mode": NRx2 written while the channel plays. This is the behaviour of CGB-02/04 from the gbdev wiki;
// on DMG only writes like $x8 (increase, period 0) are reliable, which this gives too (volume + 1)
void Envelope::ZombieWrite(unsigned char oldNrx2, unsigned char newNrx2)
{
	if ((oldNrx2 & 7) == 0 && Running) Volume++;
	else if ((oldNrx2 & 8) == 0) Volume += 2;

	if ((oldNrx2 ^ newNrx2) & 8) Volume = 16 - Volume;

	Volume &= 15;
}

Apu::Apu(int sampleRate) : _sampleRate(sampleRate)
{
	// The wiki's DMG high-pass filter charge factor is per T-cycle; apply it once per output sample instead
	_chargeFactor = std::pow(0.999958, static_cast<double>(CpuClockHz) / sampleRate);

	std::fill(std::begin(_registers), std::end(_registers), 0);
	std::copy(std::begin(InitialWaveRam), std::end(InitialWaveRam), _registers + (WaveRam - FirstRegister));
	_cyclesToEvent = CyclesToNextEvent();
}

Channel& Apu::GetChannel(int index)
{
	switch (index)
	{
	case 0:
	case 1:
		return _square[index];
	case 2:
		return _wave;
	default:
		return _noise;
	}
}

int Apu::NoisePeriod() const
{
	auto nr43 = _registers[Nr43];
	auto shift = nr43 >> 4;
	auto divisor = nr43 & 7;

	// Shifts 14 and 15 stop the LFSR
	if (shift >= 14) return 0;

	return (divisor ? divisor * 16 : 8) << shift;
}

unsigned char Apu::ReadRegister(unsigned short address) const
{
	auto reg = address - FirstRegister;

	if (address >= WaveRam)
	{
		// While CH3 plays, the CPU sees the byte CH3 is reading, and on DMG only at the moment CH3 reads it
		if (!_wave.Active) return _registers[reg];
		return WaveRamAccessible() ? _registers[(WaveRam - FirstRegister) + (_wave.Position >> 1)] : 0xff;
	}

	if (reg == Nr52)
	{
		return (_powered ? 0x80 : 0) | ReadMasks[Nr52] |
			(_square[0].Active ? 1 : 0) | (_square[1].Active ? 2 : 0) | (_wave.Active ? 4 : 0) | (_noise.Active ? 8 : 0);
	}

	return _registers[reg] | ReadMasks[reg];
}

void Apu::WriteRegister(unsigned short address, unsigned char value)
{
	CatchUp();
	Write(address - FirstRegister, value);
	_cyclesToEvent = CyclesToNextEvent();
}

void Apu::Write(int reg, unsigned char value)
{
	if (reg >= WaveRam - FirstRegister)
	{
		if (!_wave.Active) _registers[reg] = value;
		else if (WaveRamAccessible()) _registers[(WaveRam - FirstRegister) + (_wave.Position >> 1)] = value;
		return;
	}

	if (reg == Nr52)
	{
		auto power = (value & 0x80) != 0;
		if (!power && _powered) PowerOff();
		else if (power && !_powered)
		{
			_powered = true;
			_frameStep = 0;

			// SameBoy: powering on while DIV bit 4 is set skips the first frame sequencer event
			_skipFrameEvent = (_divider & 0x1000) != 0;
		}
		UpdateMix();
		return;
	}

	if (!_powered)
	{
		// DMG keeps its length counters writable while powered off (Pan Docs, gbdev wiki); nothing else is
		if (reg == Nr11 || reg == Nr21 || reg == Nr41) GetChannel(reg / 5).Length.Counter = 64 - (value & 0x3f);
		else if (reg == Nr31) _wave.Length.Counter = 256 - value;
		return;
	}

	auto old = _registers[reg];
	_registers[reg] = value;

	switch (reg)
	{
	case Nr10:
		// Leaving negate mode after a sweep calculation used it disables CH1 (Pan Docs, Audio Details)
		if (_sweepNegateUsed && (old & 8) && !(value & 8)) _square[0].Active = false;
		break;

	case Nr11:
	case Nr21:
	case Nr41:
		GetChannel(reg / 5).Length.Counter = 64 - (value & 0x3f);
		break;

	case Nr31:
		_wave.Length.Counter = 256 - value;
		break;

	case Nr12:
	case Nr22:
	{
		auto& square = _square[reg / 5];
		if (!(value & 0xf8)) square.Active = false;
		else if (square.Active) square.Env.ZombieWrite(old, value);
		break;
	}

	case Nr42:
		if (!(value & 0xf8)) _noise.Active = false;
		else if (_noise.Active) _noise.Env.ZombieWrite(old, value);
		break;

	case Nr30:
		if (!(value & 0x80)) _wave.Active = false;
		break;

	case Nr13:
	case Nr23:
	{
		auto& square = _square[reg / 5];
		square.Frequency = (square.Frequency & 0x700) | value;
		break;
	}

	case Nr14:
	case Nr24:
	{
		auto& square = _square[reg / 5];
		square.Frequency = (square.Frequency & 0xff) | (value & 7) << 8;
		WriteNrx4(reg / 5, value);
		break;
	}

	case Nr33:
		_wave.Frequency = (_wave.Frequency & 0x700) | value;
		break;

	case Nr34:
		_wave.Frequency = (_wave.Frequency & 0xff) | (value & 7) << 8;
		WriteNrx4(2, value);
		break;

	case Nr44:
		WriteNrx4(3, value);
		break;

	default:
		break;
	}

	UpdateMix();
}

void Apu::WriteNrx4(int channel, unsigned char value)
{
	auto& ch = GetChannel(channel);
	auto trigger = (value & 0x80) != 0;
	auto maxLength = channel == 2 ? 256 : 64;

	// In the first half of a length period (the next frame step doesn't clock length), enabling length clocks it
	// once, and a trigger that reloads a zero length loads one less than the maximum (gbdev wiki, Obscure Behavior)
	auto firstHalf = (_frameStep & 1) != 0;
	auto wasEnabled = ch.Length.Enabled;
	ch.Length.Enabled = (value & 0x40) != 0;

	if (firstHalf && !wasEnabled && ch.Length.Enabled && ch.Length.Counter > 0 && --ch.Length.Counter == 0 && !trigger)
	{
		ch.Active = false;
	}

	if (!trigger) return;

	if (ch.Length.Counter == 0)
	{
		ch.Length.Counter = maxLength;
		if (firstHalf && ch.Length.Enabled) ch.Length.Counter--;
	}

	if (channel < 2) TriggerSquare(channel);
	else if (channel == 2) TriggerWave();
	else TriggerNoise();
}

void Apu::TriggerSquare(int index)
{
	auto& square = _square[index];

	// A freshly started channel outputs 0 until its first duty step (Pan Docs, Audio Details)
	if (!square.Active) square.Stepped = false;
	square.Active = true;

	// The reload keeps the low two bits of the 1 MHz period counter (gbdev wiki, Obscure Behavior)
	auto counter = 2048 - (square.Timer + 3) / 4;
	auto reloaded = (square.Frequency & 0x7fc) | (counter & 3);
	square.Timer = (2048 - reloaded) * 4;

	auto nrx2 = _registers[Nr12 + index * 5];
	square.Env.Trigger(nrx2, _frameStep == 7);

	if (index == 0)
	{
		auto nr10 = _registers[Nr10];
		auto pace = (nr10 >> 4) & 7;
		_sweepShadow = square.Frequency;
		_sweepTimer = pace ? pace : 8;
		_sweepEnabled = pace != 0 || (nr10 & 7) != 0;
		_sweepNegateUsed = false;
		if (nr10 & 7) SweepCalculate();
	}

	// The other trigger effects still happen with the DAC off, but the channel stays off
	if (!SquareDacOn(index)) square.Active = false;
}

void Apu::TriggerWave()
{
	// DMG corrupts wave RAM when CH3 is retriggered just as it reads a sample (gbdev wiki, SameBoy): the first
	// bytes are overwritten with the byte, or 4-byte block, being read
	if (_wave.Active && _wave.Timer == 2)
	{
		auto offset = ((_wave.Position + 1) >> 1) & 15;
		auto wave = _registers + (WaveRam - FirstRegister);

		if (offset < 4) wave[0] = wave[offset];
		else std::copy(wave + (offset & ~3), wave + (offset & ~3) + 4, wave);
	}

	_wave.Active = true;
	_wave.Position = 0;

	// The first sample read comes 6 T-cycles later than a normal period (SameBoy, verified on hardware), and the
	// sample buffer isn't refilled, so the first sample played is the low nibble of $FF30
	_wave.Timer = _wave.Period() + 6;

	if (!WaveDacOn()) _wave.Active = false;
}

void Apu::TriggerNoise()
{
	_noise.Active = true;
	_noise.Lfsr = 0;
	_noise.Timer = NoisePeriod();
	_noise.Env.Trigger(_registers[Nr42], _frameStep == 7);

	if (!NoiseDacOn()) _noise.Active = false;
}

int Apu::SweepCalculate()
{
	auto nr10 = _registers[Nr10];
	auto delta = _sweepShadow >> (nr10 & 7);
	int result;

	if (nr10 & 8)
	{
		result = _sweepShadow - delta;
		_sweepNegateUsed = true;
	}
	else result = _sweepShadow + delta;

	if (result > 2047) _square[0].Active = false;
	return result;
}

void Apu::ClockSweep()
{
	if (--_sweepTimer > 0) return;

	auto nr10 = _registers[Nr10];
	auto pace = (nr10 >> 4) & 7;
	_sweepTimer = pace ? pace : 8;

	if (!_sweepEnabled || !pace) return;

	auto result = SweepCalculate();
	if (result <= 2047 && (nr10 & 7))
	{
		_sweepShadow = result;
		_square[0].Frequency = result;

		// Checked again with the new frequency, but not written back (gbdev wiki, Frequency Sweep)
		SweepCalculate();
	}
}

void Apu::FrameSequencerEvent()
{
	if (_skipFrameEvent)
	{
		_skipFrameEvent = false;
		return;
	}

	if (!_powered) return;

	if ((_frameStep & 1) == 0)
	{
		for (auto i = 0; i < 4; i++)
		{
			auto& ch = GetChannel(i);
			if (ch.Length.Clock()) ch.Active = false;
		}
	}

	if (_frameStep == 2 || _frameStep == 6) ClockSweep();

	if (_frameStep == 7)
	{
		if (_square[0].Active) _square[0].Env.Clock();
		if (_square[1].Active) _square[1].Env.Clock();
		if (_noise.Active) _noise.Env.Clock();
	}

	_frameStep = (_frameStep + 1) & 7;
	UpdateMix();
}

void Apu::ResetDivider()
{
	CatchUp();

	// Resetting DIV while its bit 4 is set is a falling edge, so the frame sequencer steps (Pan Docs, Audio Details)
	if (_divider & 0x1000) FrameSequencerEvent();
	_divider = 0;
	_cyclesToEvent = CyclesToNextEvent();
}

void Apu::PowerOff()
{
	// Powering off clears NR10-NR51 and all channel state, except that DMG keeps the length counters
	int lengths[4];
	for (auto i = 0; i < 4; i++) lengths[i] = GetChannel(i).Length.Counter;

	_square[0] = SquareChannel();
	_square[1] = SquareChannel();
	_wave = WaveChannel();
	_noise = NoiseChannel();

	for (auto i = 0; i < 4; i++) GetChannel(i).Length.Counter = lengths[i];

	std::fill(_registers, _registers + Nr52, 0);
	_sweepTimer = 0;
	_sweepShadow = 0;
	_sweepEnabled = false;
	_sweepNegateUsed = false;
	_powered = false;
}

void Apu::StepSquare(int index)
{
	auto& square = _square[index];
	square.DutyPosition = (square.DutyPosition + 1) & 7;
	square.Stepped = true;
	square.Timer = square.Period();
}

void Apu::StepWave()
{
	_wave.Position = (_wave.Position + 1) & 31;
	auto sample = _registers[(WaveRam - FirstRegister) + (_wave.Position >> 1)];
	_wave.SampleBuffer = _wave.Position & 1 ? sample & 0xf : sample >> 4;
	_wave.JustRead = true;
	_wave.Timer = _wave.Period();
}

void Apu::StepNoise()
{
	// Pan Docs/SameBoy convention: XNOR feedback into bit 14 (and bit 6 in 7-bit mode), output is bit 0
	auto mask = _registers[Nr43] & 8 ? 0x4040 : 0x4000;
	auto feedback = ((_noise.Lfsr ^ (_noise.Lfsr >> 1)) & 1) == 0;
	_noise.Lfsr >>= 1;
	_noise.Lfsr = feedback ? _noise.Lfsr | mask : _noise.Lfsr & ~mask;
	_noise.Timer = NoisePeriod();
}

int Apu::ChannelOutput(int channel) const
{
	switch (channel)
	{
	case 1:
	case 2:
	{
		auto index = channel - 1;
		if (!SquareDacOn(index)) return -1;

		auto& square = _square[index];
		auto duty = _registers[Nr11 + index * 5] >> 6;
		auto high = square.Stepped && (DutyPatterns[duty] >> square.DutyPosition & 1);
		return square.Active && high ? square.Env.Volume : 0;
	}

	case 3:
	{
		if (!WaveDacOn()) return -1;

		static const int Shifts[4] = { 4, 0, 1, 2 };
		return _wave.Active ? _wave.SampleBuffer >> Shifts[(_registers[Nr32] >> 5) & 3] : 0;
	}

	case 4:
		if (!NoiseDacOn()) return -1;
		return _noise.Active && (_noise.Lfsr & 1) ? _noise.Env.Volume : 0;

	default:
		return -1;
	}
}

void Apu::UpdateMix()
{
	auto nr51 = _registers[Nr51];
	auto left = 0;
	auto right = 0;

	for (auto i = 0; i < 4; i++)
	{
		auto digital = ChannelOutput(i + 1);
		if (digital < 0) continue;

		// DAC: digital 0-15 to analog +15..-15 (negative slope per Pan Docs, Audio Details); a DAC that's off gives 0
		auto analog = 15 - 2 * digital;
		if (nr51 & (0x10 << i)) left += analog;
		if (nr51 & (1 << i)) right += analog;
	}

	auto nr50 = _registers[Nr50];
	_mixLeft = left * (((nr50 >> 4) & 7) + 1);
	_mixRight = right * ((nr50 & 7) + 1);
}

void Apu::OutputSample()
{
	auto dacsOn = SquareDacOn(0) || SquareDacOn(1) || WaveDacOn() || NoiseDacOn();
	double left = 0;
	double right = 0;

	// Each sample is the average over its interval, then high-pass filtered as on the hardware (gbdev wiki); with
	// every DAC off the output is disconnected
	if (dacsOn && _sampleCycles > 0)
	{
		auto inLeft = static_cast<double>(_sumLeft) / _sampleCycles;
		auto inRight = static_cast<double>(_sumRight) / _sampleCycles;

		left = inLeft - _capacitorLeft;
		_capacitorLeft = inLeft - left * _chargeFactor;
		right = inRight - _capacitorRight;
		_capacitorRight = inRight - right * _chargeFactor;
	}

	for (auto value : { left, right })
	{
		auto pcm = std::lround(value * 32767 / MaxAmplitude);
		_samples.push_back(static_cast<short>(std::max(-32768L, std::min(32767L, pcm))));
	}

	_sumLeft = 0;
	_sumRight = 0;
	_sampleCycles = 0;
}

int Apu::CyclesToNextEvent()
{
	auto cycles = std::min(FrameSequencerPeriod - (_divider & (FrameSequencerPeriod - 1)),
		(CpuClockHz - _samplePhase + _sampleRate - 1) / _sampleRate);

	for (auto& square : _square)
	{
		if (square.Active) cycles = std::min(cycles, square.Timer);
	}

	if (_wave.Active) cycles = std::min(cycles, _wave.Timer);

	if (NoiseRunning())
	{
		// NR43 may have moved the LFSR out of its stopped state since the timer was last loaded
		if (_noise.Timer <= 0) _noise.Timer = NoisePeriod();
		cycles = std::min(cycles, _noise.Timer);
	}

	return cycles;
}

// Runs cycles up to (at most) the next event, without handling it
void Apu::Advance(int cycles)
{
	_sumLeft += static_cast<long long>(_mixLeft) * cycles;
	_sumRight += static_cast<long long>(_mixRight) * cycles;
	_sampleCycles += cycles;

	for (auto& square : _square)
	{
		if (square.Active) square.Timer -= cycles;
	}

	if (_wave.Active) _wave.Timer -= cycles;
	if (NoiseRunning()) _noise.Timer -= cycles;

	_wave.JustRead = false;
	_divider = static_cast<unsigned short>(_divider + cycles);
	_samplePhase += cycles * _sampleRate;
}

void Apu::CatchUp()
{
	if (_pendingCycles == 0) return;

	Advance(_pendingCycles);
	_pendingCycles = 0;
}

void Apu::RunEvents(int cycles)
{
	// Event-driven: run up to the next channel step, frame sequencer step or output sample, whichever is first
	cycles += _pendingCycles;
	_pendingCycles = 0;

	while (cycles > 0)
	{
		auto run = CyclesToNextEvent();
		if (cycles < run)
		{
			// The rest ends before the next event
			_pendingCycles = cycles;
			_cyclesToEvent = run;
			return;
		}

		Advance(run);
		cycles -= run;

		auto changed = false;
		for (auto i = 0; i < 2; i++)
		{
			if (_square[i].Active && _square[i].Timer == 0)
			{
				StepSquare(i);
				changed = true;
			}
		}

		if (_wave.Active && _wave.Timer == 0)
		{
			StepWave();
			changed = true;
		}

		if (NoiseRunning() && _noise.Timer == 0)
		{
			StepNoise();
			changed = true;
		}

		if ((_divider & (FrameSequencerPeriod - 1)) == 0) FrameSequencerEvent();

		if (_samplePhase >= CpuClockHz)
		{
			_samplePhase -= CpuClockHz;
			OutputSample();
		}

		if (changed) UpdateMix();
	}

	_cyclesToEvent = CyclesToNextEvent();
}

void Apu::TakeSamples(std::vector<short>& out)
{
	out.clear();
	out.swap(_samples);
}
