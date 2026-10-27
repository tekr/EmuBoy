#pragma once
#include <ostream>
#include <string>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// Runs ROMs from Gekkio's mooneye-test-suite (https://github.com/Gekkio/mooneye-test-suite; see roms/readme.md).
// A ROM reports its result by executing LD B,B with B, C, D, E, H and L holding 3, 5, 8, 13, 21 and 34 if it
// passed, or all $42 if it failed, then loops forever.
class MooneyeRomRunner
{
public:
	enum class Outcome
	{
		RomNotFound,
		TimedOut,
		Passed,
		Failed
	};

	struct Result
	{
		MooneyeRomRunner::Outcome Outcome;
		::Registers Registers;
	};

	// The slowest MBC tests take around 12 seconds of emulated time; a minute leaves plenty of margin
	static Result Run(const std::string& romPath, int maxFrames = 60 * 60)
	{
		auto cartridge = CartridgeFactory::LoadFromFile(romPath);
		if (cartridge == nullptr) return { Outcome::RomNotFound, {} };

		static const unsigned char passSignature[] = { 3, 5, 8, 13, 21, 34 };
		static const unsigned char failSignature[] = { 0x42, 0x42, 0x42, 0x42, 0x42, 0x42 };

		Emulator emulator{ cartridge };
		auto& cpu = emulator.GetCpu();

		for (auto frame = 0; frame < maxFrames; frame++)
		{
			emulator.GetFrame();

			// Checked between frames, so the registers must hold a signature too: a ROM can hit LD B,B earlier
			// while still reporting, but it stays at its final LD B,B with the result in its registers
			if (!cpu.SoftwareBreakpointHit()) continue;

			auto& registers = cpu.GetRegisters();
			if (HasSignature(registers, passSignature)) return { Outcome::Passed, registers };
			if (HasSignature(registers, failSignature)) return { Outcome::Failed, registers };
		}

		return { Outcome::TimedOut, cpu.GetRegisters() };
	}

private:
	static bool HasSignature(const Registers& registers, const unsigned char (&signature)[6])
	{
		return registers.B == signature[0] && registers.C == signature[1] && registers.D == signature[2] &&
			registers.E == signature[3] && registers.H == signature[4] && registers.L == signature[5];
	}
};

inline std::ostream& operator<<(std::ostream& os, const MooneyeRomRunner::Result& result)
{
	static const char* outcomes[] = { "ROM not found (run the tests from src/tests)", "timed out", "passed", "failed" };

	auto& r = result.Registers;
	return os << outcomes[static_cast<int>(result.Outcome)] << " with BC=" << std::hex << r.BC << " DE=" << r.DE
		<< " HL=" << r.HL << " PC=" << r.PC;
}
