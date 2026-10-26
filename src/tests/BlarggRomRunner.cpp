#include "stdafx.h"
#include "BlarggRomRunner.h"
#include <gtest/gtest.h>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

namespace
{
	const unsigned short ResultAddress = 0xa000;
	const unsigned short TextAddress = 0xa004;
	const unsigned char RunningStatus = 0x80;

	bool HasRamSignature(MemoryMap& memory)
	{
		return memory.ReadByte(0xa001) == 0xde && memory.ReadByte(0xa002) == 0xb0 && memory.ReadByte(0xa003) == 0x61;
	}

	std::string ReadRamText(MemoryMap& memory)
	{
		std::string text;

		for (auto address = TextAddress; address < MemoryMap::RamFixed && memory.ReadByte(address) != 0; address++)
		{
			text += static_cast<char>(memory.ReadByte(address));
		}

		return text;
	}

	bool EndsWith(const std::string& text, const std::string& suffix)
	{
		return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
	}
}

BlarggRomResult RunBlarggRom(const std::string& path, int maxFrames, const BetweenFramesHook& betweenFrames)
{
	BlarggRomResult result;

	auto cartridge = CartridgeFactory::LoadFromFile(path, 1);
	if (cartridge == nullptr) return result;
	result.RomFound = true;

	Emulator emulator{ cartridge };
	auto& memory = emulator.GetMemoryMap();

	std::string serialText;
	emulator.GetSerial().SetByteSentHandler([&serialText](unsigned char byte) { serialText += static_cast<char>(byte); });

	for (result.Frames = 1; result.Frames <= maxFrames; result.Frames++)
	{
		emulator.GetFrame();
		if (betweenFrames) betweenFrames(memory);

		// The serial output's last line is "Passed", or "Failed" possibly followed by a test number
		if (EndsWith(serialText, "Passed\n") || serialText.find("Failed") != std::string::npos)
		{
			// Let a failure message finish printing
			for (auto i = 0; i < 10; i++) emulator.GetFrame();

			result.Finished = true;
			result.Passed = serialText.find("Failed") == std::string::npos;
			result.Output = serialText;
			return result;
		}

		if (HasRamSignature(memory) && memory.ReadByte(ResultAddress) != RunningStatus)
		{
			result.Finished = true;
			result.Passed = memory.ReadByte(ResultAddress) == 0;
			result.Output = ReadRamText(memory);
			return result;
		}
	}

	result.Frames = maxFrames;
	result.Output = serialText;
	return result;
}

void ExpectBlarggRomPasses(const std::string& path, int maxFrames, const BetweenFramesHook& betweenFrames)
{
	SCOPED_TRACE(path);
	auto result = RunBlarggRom(path, maxFrames, betweenFrames);

	ASSERT_TRUE(result.RomFound) << "ROM not found; run the tests from src/tests";
	ASSERT_TRUE(result.Finished) << "No result after " << maxFrames << " frames. Output so far:\n" << result.Output;
	EXPECT_TRUE(result.Passed) << result.Output;
}
