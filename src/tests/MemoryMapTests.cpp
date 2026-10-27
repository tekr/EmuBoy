#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include "../core/MemoryMap.h"
#include "../core/InputJoypad.h"

TEST(MemoryMapTest, WorkRamAndHighRamStartZeroed)
{
	// Their contents at power-on are random on hardware, but zeroed for repeatability: left uninitialised, a new
	// memory map took whatever its memory held before, such as an earlier emulator's results. It's built over
	// memory filled with $AA to show that
	static_assert(alignof(MemoryMap) <= alignof(std::max_align_t), "new[] doesn't align the memory map");
	std::unique_ptr<unsigned char[]> storage{ new unsigned char[sizeof(MemoryMap)] };
	memset(storage.get(), 0xaa, sizeof(MemoryMap));

	InputJoypad joypad;
	auto memoryMap = new (storage.get()) MemoryMap{ joypad };

	for (unsigned address = MemoryMap::RamFixed; address < MemoryMap::RamFixed + MemoryMap::RamBankSize; address++)
	{
		ASSERT_EQ(0, memoryMap->ReadByte(static_cast<unsigned short>(address))) << std::hex << address;
	}

	// Including the interrupt enable register at $FFFF, which is kept with high RAM
	for (unsigned address = MemoryMap::HighRam; address <= 0xffff; address++)
	{
		ASSERT_EQ(0, memoryMap->ReadByte(static_cast<unsigned short>(address))) << std::hex << address;
	}

	memoryMap->~MemoryMap();
}
