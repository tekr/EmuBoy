#pragma once
#include <vector>

// Original Game Boy (DMG) sound. References: Pan Docs (Audio, Audio Registers, Audio Details) and the gbdev wiki
// "Gameboy sound hardware"; SameBoy's apu.c where those are silent.

struct LengthCounter
{
	int Counter = 0;
	bool Enabled = false;

	// Returns true when the counter expires, which disables the channel
	bool Clock() { return Enabled && Counter > 0 && --Counter == 0; }
};

struct Envelope
{
	int Volume = 0;
	int Timer = 0;
	int Period = 0;
	bool Increase = false;
	bool Running = false;

	// A period of 0 counts as 8 for the timer, though the volume then never changes
	int TimerPeriod() const { return Period ? Period : 8; }

	void Trigger(unsigned char nrx2, bool nextStepClocksEnvelope);
	void Clock();
	void ZombieWrite(unsigned char oldNrx2, unsigned char newNrx2);
};

struct Channel
{
	bool Active = false;
	int Timer = 0;
	LengthCounter Length;
};

struct SquareChannel : Channel
{
	int Frequency = 0;
	int DutyPosition = 0;
	bool Stepped = false;
	Envelope Env;

	int Period() const { return (2048 - Frequency) * 4; }
};

struct WaveChannel : Channel
{
	int Frequency = 0;
	int Position = 0;
	unsigned char SampleBuffer = 0;
	bool JustRead = false;

	int Period() const { return (2048 - Frequency) * 2; }
};

struct NoiseChannel : Channel
{
	unsigned short Lfsr = 0;
	Envelope Env;
};

class Apu
{
public:
	static const int CpuClockHz = 4194304;

private:
	static const unsigned short FirstRegister = 0xff10;
	static const int RegisterCount = 0x30;

	// Register offsets from FirstRegister. Each channel has 5 register slots, NRx0-NRx4, so reg / 5 is its channel
	static const int RegistersPerChannel = 5;

	static const int Nr10 = 0x00, Nr11 = 0x01, Nr12 = 0x02, Nr13 = 0x03, Nr14 = 0x04;
	static const int Nr21 = 0x06, Nr22 = 0x07, Nr23 = 0x08, Nr24 = 0x09;
	static const int Nr30 = 0x0a, Nr31 = 0x0b, Nr32 = 0x0c, Nr33 = 0x0d, Nr34 = 0x0e;
	static const int Nr41 = 0x10, Nr42 = 0x11, Nr43 = 0x12, Nr44 = 0x13;
	static const int Nr50 = 0x14, Nr51 = 0x15, Nr52 = 0x16;
	static const int WaveRam = 0x20;

	static const int Ch1 = 0, Ch2 = 1, Ch3 = 2, Ch4 = 3, ChannelCount = 4;

	// The frame sequencer steps on falling edges of DIV bit 4, which is bit 12 of the internal 16-bit divider
	static const unsigned short DivBit4 = 0x1000;
	static const int FrameSequencerPeriod = 8192;
	static const int EnvelopeStep = 7;

	static const unsigned char SweepNegateBit = 0x08;

	// Frequencies are 11 bits; a sweep calculation above this disables CH1
	static const int MaxFrequency = 2047;

	// Four channels, each with a DAC range of +-15, scaled by an NR50 volume of up to 8
	static const int MaxAmplitude = ChannelCount * 15 * 8;

	static const unsigned char ReadMasks[RegisterCount];
	static const unsigned char DutyPatterns[4];
	static const unsigned char InitialWaveRam[16];
	static const int WaveVolumeShifts[4];

	int _sampleRate;
	double _chargeFactor;

	unsigned char _registers[RegisterCount];
	bool _powered = false;

	unsigned short _divider = 0;
	int _frameStep = 0;
	bool _skipFrameEvent = false;

	SquareChannel _square[2];
	WaveChannel _wave;
	NoiseChannel _noise;

	int _sweepTimer = 0;
	int _sweepShadow = 0;
	bool _sweepEnabled = false;
	bool _sweepNegateUsed = false;

	int _samplePhase = 0;
	int _sampleCycles = 0;
	long long _sumLeft = 0;
	long long _sumRight = 0;
	int _mixLeft = 0;
	int _mixRight = 0;
	double _capacitorLeft = 0;
	double _capacitorRight = 0;
	std::vector<short> _samples;

	// Cycles run since the channel state was last brought up to date. Kept below _cyclesToEvent, so nothing
	// observable has happened in them yet
	int _pendingCycles = 0;
	int _cyclesToEvent = 0;

	bool DacOn(int channel) const;
	int PlayingWaveByte() const { return WaveRam + (_wave.Position >> 1); }
	bool WaveRamAccessible() const { return !_wave.Active || (_wave.JustRead && _pendingCycles == 0); }
	bool NoiseRunning() const { return _noise.Active && NoisePeriod() > 0; }
	Channel& GetChannel(int index);
	int& Frequency(int channel) { return channel == Ch3 ? _wave.Frequency : _square[channel].Frequency; }
	int NoisePeriod() const;
	int SweepPace() const { return (_registers[Nr10] >> 4) & 7; }
	int SweepShift() const { return _registers[Nr10] & 7; }
	bool SweepNegate() const { return (_registers[Nr10] & SweepNegateBit) != 0; }
	int SweepTimerPeriod() const;
	static int MaxLength(int channel) { return channel == Ch3 ? 256 : 64; }

	int CyclesToNextEvent();
	void Advance(int cycles);
	void CatchUp();
	void RunEvents(int cycles);
	void Write(int reg, unsigned char value);
	void PowerOff();
	void WriteLength(int channel, unsigned char value);
	void WriteNrx4(int channel, unsigned char value);
	void TriggerSquare(int index);
	void TriggerWave();
	void TriggerNoise();
	int SweepCalculate();
	void ClockSweep();
	void FrameSequencerEvent();
	void StepSquare(int index);
	void StepWave();
	void StepNoise();
	void UpdateMix();
	void OutputSample();

public:
	explicit Apu(int sampleRate = 44100);

	// address is the full CPU address, 0xff10-0xff3f (includes unused addresses in that range)
	unsigned char ReadRegister(unsigned short address) const;
	void WriteRegister(unsigned short address, unsigned char value);

	// T-cycles (4194304 Hz), includes cycles while the CPU is halted
	void RunCycles(int cycles)
	{
		// Called for every instruction, most of which end before the next APU event: just note the time
		if (_pendingCycles + cycles < _cyclesToEvent) _pendingCycles += cycles;
		else RunEvents(cycles);
	}

	// DIV (0xff04) was written: apply the frame sequencer's DIV falling-edge rule
	void ResetDivider();

	// Interleaved stereo signed 16-bit samples produced since the last call; moves them into `out` (cleared first)
	void TakeSamples(std::vector<short>& out);

	// For tests and debugging: current digital output (0-15) of channel 1-4, or -1 if that channel's DAC is off
	int ChannelOutput(int channel) const;

	int GetSampleRate() const { return _sampleRate; }
};
