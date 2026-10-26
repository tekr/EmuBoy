#include "stdafx.h"
#include <gtest/gtest.h>
#include "TestMemoryMap.h"
#include "../core/InputJoypad.h"

// I/O register values the DMG boot ROM leaves, per mooneye-test-suite's boot_hwio-dmgABCmgb and unused_hwio-GS

TEST(IoPortTest, JoypadSelectsBothKeyRowsAtPowerOn)
{
	InputJoypad joypad;
	TestMemoryMap memoryMap{ joypad };

	EXPECT_EQ(0xcf, memoryMap.ReadByte(MemoryMap::JoypadPort));
}

TEST(IoPortTest, UnmappedPortsReadFF)
{
	InputJoypad joypad;
	TestMemoryMap memoryMap{ joypad };

	for (unsigned short address = MemoryMap::UnusableArea2; address < MemoryMap::HighRam; address++)
	{
		memoryMap.WriteByte(address, 0);
		if (address != MemoryMap::InternalRomDisable) EXPECT_EQ(0xff, memoryMap.ReadByte(address)) << std::hex << address;
	}
}
