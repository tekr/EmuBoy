#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/MemoryMap.h"
#include "../core/Mbc1Cartridge.h"
#include "../core/Mbc2Cartridge.h"
#include "../core/Mbc3Cartridge.h"
#include "../core/RealTimeClock.h"

class CartridgeRamTest : public testing::Test
{
public:
	InputJoypad Joypad;
	MemoryMap Memory{ Joypad };
	std::shared_ptr<Cartridge> Mbc1{ std::make_shared<Mbc1Cartridge>(std::vector<unsigned char>(2 * MemoryMap::RomBankSize),
		Cartridge::RamBankSize, true) };

	CartridgeRamTest()
	{
		Memory.SetCartridge(Mbc1);
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

	// Disabled RAM reads as open bus
	Memory.WriteByte(0x0000, 0x0b);
	EXPECT_EQ(0xff, Memory.ReadByte(0xa000));

	Memory.WriteByte(0x0000, 0x1a);
	EXPECT_EQ(0x12, Memory.ReadByte(0xa000));
}

// The unsaved-changes flag tells the front end when battery-backed RAM needs saving

TEST_F(CartridgeRamTest, StartsWithNoUnsavedChanges)
{
	EXPECT_FALSE(Mbc1->HasUnsavedChanges());
}

TEST_F(CartridgeRamTest, RamWriteMarksUnsavedChangesUntilSaved)
{
	Memory.WriteByte(0x0000, 0x0a);
	Memory.WriteByte(0xa123, 0x45);
	EXPECT_TRUE(Mbc1->HasUnsavedChanges());

	Mbc1->MarkSaved();
	EXPECT_FALSE(Mbc1->HasUnsavedChanges());
}

TEST_F(CartridgeRamTest, RewritingTheSameValueIsNotAChange)
{
	Memory.WriteByte(0x0000, 0x0a);
	Memory.WriteByte(0xa123, 0x45);
	Mbc1->MarkSaved();

	Memory.WriteByte(0xa123, 0x45);
	EXPECT_FALSE(Mbc1->HasUnsavedChanges());
}

TEST_F(CartridgeRamTest, IgnoredWriteIsNotAChange)
{
	Memory.WriteByte(0xa123, 0x45);

	EXPECT_FALSE(Mbc1->HasUnsavedChanges());
}

TEST(UnsavedChangesTest, Mbc2RamWriteIsAChange)
{
	Mbc2Cartridge cartridge{ std::vector<unsigned char>(0x8000), true };
	cartridge.RomWriteByte(0x0000, 0x0a);

	cartridge.RamWriteByte(0x0010, 0x07);

	EXPECT_TRUE(cartridge.HasUnsavedChanges());
}

TEST(UnsavedChangesTest, Mbc3ClockRegisterWriteIsAChange)
{
	Mbc3Cartridge cartridge{ std::vector<unsigned char>(0x8000), 0, true, true };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RomWriteByte(0x4000, 0x08 + RealTimeClock::Minutes);

	cartridge.RamWriteByte(0x0000, 0x15);

	EXPECT_TRUE(cartridge.HasUnsavedChanges());
}

TEST(UnsavedChangesTest, ClockTickingIsNotAChange)
{
	// The save records when it was written, so time passing needn't be saved
	Mbc3Cartridge cartridge{ std::vector<unsigned char>(0x8000), 0, true, true };

	cartridge.RunCycles(2 * RealTimeClock::CyclesPerSecond);

	EXPECT_FALSE(cartridge.HasUnsavedChanges());
}
