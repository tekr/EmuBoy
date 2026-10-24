#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/MemoryMap.h"
#include "../core/Cartridge.h"

class CartridgeRamTest : public testing::Test
{
public:
	InputJoypad Joypad;
	MemoryMap Memory{ Joypad };

	CartridgeRamTest()
	{
		Memory.SetCartridge(std::make_shared<Cartridge>(std::vector<unsigned char>(2 * MemoryMap::RomBankSize), 1));
	}
};

TEST_F(CartridgeRamTest, WritesAndReadsBackWhenEnabled)
{
	Memory.WriteByte(0x0000, 0x0a);
	Memory.WriteByte(0xa000, 0x12);
	Memory.WriteByte(0xbfff, 0x34);

	EXPECT_EQ(0x12, Memory.ReadByte(0xa000));
	EXPECT_EQ(0x34, Memory.ReadByte(0xbfff));
}

TEST_F(CartridgeRamTest, IgnoresWritesWhenDisabled)
{
	Memory.WriteByte(0x0000, 0x0a);
	Memory.WriteByte(0xa000, 0x12);
	Memory.WriteByte(0x0000, 0x00);
	Memory.WriteByte(0xa000, 0x56);
	Memory.WriteByte(0x0000, 0x0a);

	EXPECT_EQ(0x12, Memory.ReadByte(0xa000));
}

TEST_F(CartridgeRamTest, EnabledOnlyByLowNibbleOfA)
{
	Memory.WriteByte(0x0000, 0x0a);
	Memory.WriteByte(0xa000, 0x12);

	Memory.WriteByte(0x0000, 0x0b);
	EXPECT_EQ(0x0, Memory.ReadByte(0xa000));

	Memory.WriteByte(0x0000, 0x1a);
	EXPECT_EQ(0x12, Memory.ReadByte(0xa000));
}
