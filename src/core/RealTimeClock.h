#pragma once
#include <array>
#include <cstdint>

// The MBC3's real-time clock, per https://gbdev.io/pandocs/MBC3.html. It counts emulated time (clock cycles), so it
// is deterministic; AdvanceSeconds lets it catch up with real time that passed while the emulator wasn't running.
class RealTimeClock
{
public:
	static const int CyclesPerSecond = 4194304;

	// Register numbers, selected by writing $08-$0C to the MBC3's RAM bank register
	enum Register
	{
		Seconds,
		Minutes,
		Hours,
		DaysLow,
		DaysHigh,

		RegisterCount
	};

	// DaysHigh bits
	enum : unsigned char
	{
		DayHighBit = 0x01,
		HaltBit = 0x40,
		DayCarryBit = 0x80
	};

	// Everything needed to save and restore the clock
	struct State
	{
		std::array<unsigned char, RegisterCount> Counters{};
		std::array<unsigned char, RegisterCount> Latched{};
		int CyclesIntoSecond{ 0 };
		bool LatchArmed{ false };
	};

private:
	State _state;

	bool IsHalted() const { return (_state.Counters[DaysHigh] & HaltBit) != 0; }
	bool CountersInRange() const;
	void Tick();

public:
	void RunCycles(int cycles);

	// Moves the clock on by whole seconds, unless it is halted
	void AdvanceSeconds(uint64_t seconds);

	// Writing 0 then 1 copies the counters to the latched registers that the game reads
	void WriteLatch(unsigned char value);

	unsigned char ReadRegister(int reg) const { return _state.Latched[reg]; }
	void WriteRegister(int reg, unsigned char value);

	const State& GetState() const { return _state; }

	// Restores a saved state, keeping only bits the hardware has, so a corrupt state can't produce impossible values
	void SetState(const State& state);
};
