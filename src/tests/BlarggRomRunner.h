#pragma once
#include <functional>
#include <string>

class MemoryMap;

// Called between frames, for a test to adjust the ROM's memory
using BetweenFramesHook = std::function<void(MemoryMap&)>;

// Runs one of blargg's Game Boy test ROMs (see roms/readme.md) until it reports a result. Older ROMs send their
// text output through the serial port and finish with "Passed" or "Failed". Newer ones also write to cartridge
// RAM: $A000 holds 0x80 while the test runs, then the result code (0 = passed), with the signature DE B0 61 at
// $A001-$A003 and the text output as a zero-terminated string from $A004.
struct BlarggRomResult
{
	bool RomFound = false;
	bool Finished = false;
	bool Passed = false;
	int Frames = 0;

	// The ROM's text output, from cartridge RAM if it reports there, otherwise from the serial port
	std::string Output;
};

BlarggRomResult RunBlarggRom(const std::string& path, int maxFrames, const BetweenFramesHook& betweenFrames = nullptr);

// Google Test assertion that the ROM reports success within maxFrames, showing its output if not
void ExpectBlarggRomPasses(const std::string& path, int maxFrames, const BetweenFramesHook& betweenFrames = nullptr);
