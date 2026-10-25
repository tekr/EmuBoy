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

// CH3 output level (NR32 bits 5-6): mute, 100%, 50%, 25%
const int Apu::WaveVolumeShifts[4] = { 4, 0, 1, 2 };

void Envelope::Trigger(unsigned char nrx2, bool nextStepClocksEnvelope)
{
	Volume = nrx2 >> 4;
	Increase = (nrx2 & 8) != 0;
	Period = nrx2 & 7;
	Running = true;

	// A trigger just before an envelope step delays it by one (Pan Docs, Audio Details)
	Timer = TimerPeriod() + (nextStepClocksEnvelope ? 1 : 0);
}

void Envelope::Clock()
{
	if (--Timer > 0) return;

	Timer = TimerPeriod();
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
	std::copy(std::begin(InitialWaveRam), std::end(InitialWaveRam), _registers + WaveRam);
	_cyclesToEvent = CyclesToNextEvent();
}

Channel& Apu::GetChannel(int index)
{
	switch (index)
	{
	case Ch1:
	case Ch2:
		return _square[index];
	case Ch3:
		return _wave;
	default:
		return _noise;
	}
}

// CH3 has its own DAC switch. The others' DACs are off when NRx2 gives initial volume 0, decreasing
bool Apu::DacOn(int channel) const
{
	if (channel == Ch3) return (_registers[Nr30] & 0x80) != 0;
	return (_registers[Nr12 + channel * RegistersPerChannel] & 0xf8) != 0;
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

	if (reg >= WaveRam)
	{
		// While CH3 plays, the CPU sees the byte CH3 is reading, and on DMG only at the moment CH3 reads it
		if (!_wave.Active) return _registers[reg];
		return WaveRamAccessible() ? _registers[PlayingWaveByte()] : 0xff;
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
	if (reg >= WaveRam)
	{
		if (!_wave.Active) _registers[reg] = value;
		else if (WaveRamAccessible()) _registers[PlayingWaveByte()] = value;
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
			_skipFrameEvent = (_divider & DivBit4) != 0;
		}
		UpdateMix();
		return;
	}

	if (!_powered)
	{
		// DMG keeps its length counters writable while powered off (Pan Docs, gbdev wiki); nothing else is
		if (reg == Nr11 || reg == Nr21 || reg == Nr31 || reg == Nr41) WriteLength(reg / RegistersPerChannel, value);
		return;
	}

	auto old = _registers[reg];
	_registers[reg] = value;

	switch (reg)
	{
	case Nr10:
		// Leaving negate mode after a sweep calculation used it disables CH1 (Pan Docs, Audio Details)
		if (_sweepNegateUsed && (old & 8) && !(value & 8)) _square[Ch1].Active = false;
		break;

	case Nr11:
	case Nr21:
	case Nr31:
	case Nr41:
		WriteLength(reg / RegistersPerChannel, value);
		break;

	case Nr12:
	case Nr22:
	case Nr42:
	{
		auto channel = reg / RegistersPerChannel;
		auto& envelope = channel == Ch4 ? _noise.Env : _square[channel].Env;
		auto& ch = GetChannel(channel);
		if (!DacOn(channel)) ch.Active = false;
		else if (ch.Active) envelope.ZombieWrite(old, value);
		break;
	}

	case Nr30:
		if (!DacOn(Ch3)) _wave.Active = false;
		break;

	case Nr13:
	case Nr23:
	case Nr33:
	{
		auto& frequency = Frequency(reg / RegistersPerChannel);
		frequency = (frequency & 0x700) | value;
		break;
	}

	case Nr14:
	case Nr24:
	case Nr34:
	{
		auto& frequency = Frequency(reg / RegistersPerChannel);
		frequency = (frequency & 0xff) | (value & 7) << 8;
		WriteNrx4(reg / RegistersPerChannel, value);
		break;
	}

	case Nr44:
		WriteNrx4(Ch4, value);
		break;

	default:
		break;
	}

	UpdateMix();
}

void Apu::WriteLength(int channel, unsigned char value)
{
	auto maxLength = MaxLength(channel);
	GetChannel(channel).Length.Counter = maxLength - (value & (maxLength - 1));
}

void Apu::WriteNrx4(int channel, unsigned char value)
{
	auto& ch = GetChannel(channel);
	auto trigger = (value & 0x80) != 0;

	// When the next frame step doesn't clock length, enabling length clocks it once, and a trigger that reloads a
	// zero length loads one less than the maximum (gbdev wiki, Obscure Behavior)
	auto nextStepSkipsLength = (_frameStep & 1) != 0;
	auto wasEnabled = ch.Length.Enabled;
	ch.Length.Enabled = (value & 0x40) != 0;

	if (nextStepSkipsLength && !wasEnabled && ch.Length.Enabled && ch.Length.Counter > 0 && --ch.Length.Counter == 0 && !trigger)
	{
		ch.Active = false;
	}

	if (!trigger) return;

	if (ch.Length.Counter == 0)
	{
		ch.Length.Counter = MaxLength(channel);
		if (nextStepSkipsLength && ch.Length.Enabled) ch.Length.Counter--;
	}

	if (channel <= Ch2) TriggerSquare(channel);
	else if (channel == Ch3) TriggerWave();
	else TriggerNoise();

	// The other trigger effects still happen with the DAC off, but the channel stays off
	if (!DacOn(channel)) ch.Active = false;
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

	auto nrx2 = _registers[Nr12 + index * RegistersPerChannel];
	square.Env.Trigger(nrx2, _frameStep == EnvelopeStep);

	if (index == Ch1)
	{
		_sweepShadow = square.Frequency;
		_sweepTimer = SweepTimerPeriod();
		_sweepEnabled = SweepPace() != 0 || SweepShift() != 0;
		_sweepNegateUsed = false;
		if (SweepShift()) SweepCalculate();
	}
}

void Apu::TriggerWave()
{
	// DMG corrupts wave RAM when CH3 is retriggered in the 2 MHz tick before it reads a sample (gbdev wiki;
	// SameBoy's sample_countdown == 0, counted in 2 MHz ticks): the first bytes are overwritten with the byte, or
	// 4-byte block, being read
	if (_wave.Active && _wave.Timer <= 2)
	{
		auto offset = ((_wave.Position + 1) >> 1) & 15;
		auto wave = _registers + WaveRam;

		if (offset < 4) wave[0] = wave[offset];
		else std::copy(wave + (offset & ~3), wave + (offset & ~3) + 4, wave);
	}

	_wave.Active = true;
	_wave.Position = 0;

	// The first sample read comes 6 T-cycles later than a normal period (SameBoy, verified on hardware), and the
	// sample buffer isn't refilled, so the first sample played is the low nibble of $FF30
	_wave.Timer = _wave.Period() + 6;
}

void Apu::TriggerNoise()
{
	_noise.Active = true;
	_noise.Lfsr = 0;
	_noise.Timer = NoisePeriod();
	_noise.Env.Trigger(_registers[Nr42], _frameStep == EnvelopeStep);
}

// A sweep pace of 0 counts as 8 for the timer, though the sweep then never changes the frequency
int Apu::SweepTimerPeriod() const
{
	auto pace = SweepPace();
	return pace ? pace : 8;
}

int Apu::SweepCalculate()
{
	auto delta = _sweepShadow >> SweepShift();
	int result;

	if (_registers[Nr10] & 8)
	{
		result = _sweepShadow - delta;
		_sweepNegateUsed = true;
	}
	else result = _sweepShadow + delta;

	if (result > MaxFrequency) _square[Ch1].Active = false;
	return result;
}

void Apu::ClockSweep()
{
	if (--_sweepTimer > 0) return;

	_sweepTimer = SweepTimerPeriod();

	if (!_sweepEnabled || !SweepPace()) return;

	auto result = SweepCalculate();
	if (result <= MaxFrequency && SweepShift())
	{
		_sweepShadow = result;
		_square[Ch1].Frequency = result;

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
		for (auto i = 0; i < ChannelCount; i++)
		{
			auto& ch = GetChannel(i);
			if (ch.Length.Clock()) ch.Active = false;
		}
	}

	if (_frameStep == 2 || _frameStep == 6) ClockSweep();

	if (_frameStep == EnvelopeStep)
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
	if (_divider & DivBit4) FrameSequencerEvent();
	_divider = 0;
	_cyclesToEvent = CyclesToNextEvent();
}

void Apu::PowerOff()
{
	// Powering off clears NR10-NR51 and all channel state, except that DMG keeps the length counters
	int lengths[ChannelCount];
	for (auto i = 0; i < ChannelCount; i++) lengths[i] = GetChannel(i).Length.Counter;

	_square[0] = SquareChannel();
	_square[1] = SquareChannel();
	_wave = WaveChannel();
	_noise = NoiseChannel();

	for (auto i = 0; i < ChannelCount; i++) GetChannel(i).Length.Counter = lengths[i];

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
	auto sample = _registers[PlayingWaveByte()];
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
	auto index = channel - 1;
	if (index < Ch1 || index > Ch4 || !DacOn(index)) return -1;

	switch (index)
	{
	case Ch1:
	case Ch2:
	{
		auto& square = _square[index];
		auto duty = _registers[Nr11 + index * RegistersPerChannel] >> 6;
		auto high = square.Stepped && (DutyPatterns[duty] >> square.DutyPosition & 1);
		return square.Active && high ? square.Env.Volume : 0;
	}

	case Ch3:
		return _wave.Active ? _wave.SampleBuffer >> WaveVolumeShifts[(_registers[Nr32] >> 5) & 3] : 0;

	default:
		return _noise.Active && (_noise.Lfsr & 1) ? _noise.Env.Volume : 0;
	}
}

void Apu::UpdateMix()
{
	auto nr51 = _registers[Nr51];
	auto left = 0;
	auto right = 0;

	for (auto i = 0; i < ChannelCount; i++)
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
	auto dacsOn = DacOn(Ch1) || DacOn(Ch2) || DacOn(Ch3) || DacOn(Ch4);
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

	// After the high-pass filter the signal swings up to twice the DAC range (a full-scale step from one extreme
	// to the other), so scale by twice the maximum to leave that headroom
	for (auto value : { left, right })
	{
		auto pcm = std::lround(value * 32767 / (2 * MaxAmplitude));
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
			_pendingCycles = cycles;
			_cyclesToEvent = run;
			return;
		}

		Advance(run);
		cycles -= run;

		auto changed = false;
		for (auto i = Ch1; i <= Ch2; i++)
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
