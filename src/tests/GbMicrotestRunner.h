#pragma once
#include <ostream>
#include <string>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// Runs ROMs from aappleby's gbmicrotest (https://github.com/aappleby/gbmicrotest; see roms/readme.md). A ROM tests
// one register at one machine cycle, then leaves the value it read at $FF80, the value expected at $FF81, and $01 at
// $FF82 if they match or $FF if they don't
class GbMicrotestRunner
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
		GbMicrotestRunner::Outcome Outcome;
		unsigned char Actual;
		unsigned char Expected;
	};

	// The boot ROM takes around 333 frames (see EmulatorTimingTests), and the tests finish within a few after it
	static Result Run(const std::string& romPath, int maxFrames = 400)
	{
		auto cartridge = CartridgeFactory::LoadFromFile(romPath);
		if (cartridge == nullptr) return { Outcome::RomNotFound, 0, 0 };

		const unsigned short actualAddress = 0xff80;
		const unsigned short expectedAddress = 0xff81;
		const unsigned short verdictAddress = 0xff82;
		const unsigned char passed = 0x01;
		const unsigned char failed = 0xff;

		// High RAM starts zeroed, so the verdict can't be mistaken for one left from before
		Emulator emulator{ cartridge };
		auto& memoryMap = emulator.GetMemoryMap();

		for (auto frame = 0; frame < maxFrames; frame++)
		{
			emulator.GetFrame();

			auto verdict = memoryMap.ReadByte(verdictAddress);
			if (verdict != passed && verdict != failed) continue;

			return { verdict == passed ? Outcome::Passed : Outcome::Failed, memoryMap.ReadByte(actualAddress),
				memoryMap.ReadByte(expectedAddress) };
		}

		return { Outcome::TimedOut, memoryMap.ReadByte(actualAddress), memoryMap.ReadByte(expectedAddress) };
	}
};

inline std::ostream& operator<<(std::ostream& os, const GbMicrotestRunner::Result& result)
{
	static const char* outcomes[] = { "ROM not found (run the tests from src/tests)", "timed out", "passed", "failed" };

	os << outcomes[static_cast<int>(result.Outcome)];
	if (result.Outcome == GbMicrotestRunner::Outcome::RomNotFound) return os;

	return os << std::hex << ", reading $" << +result.Actual << " where $" << +result.Expected << " was expected";
}
