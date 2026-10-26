#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/Mbc5Cartridge.h"
#include "CartridgeTestHelpers.h"

using namespace CartridgeTestHelpers;

// Expectations from https://gbdev.io/pandocs/MBC5.html

namespace
{
	const size_t RamBankSize = Cartridge::RamBankSize;
}

TEST(Mbc5Test, StartsWithBanks0And1)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 0 };

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc5Test, SelectsLowRomBankBitsIn2000To2fff)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(512), 0 };

	cartridge.RomWriteByte(0x2000, 0x23);
	EXPECT_EQ(0x23, Bank1(cartridge));

	cartridge.RomWriteByte(0x2fff, 0xff);
	EXPECT_EQ(0xff, Bank1(cartridge));
	EXPECT_EQ(0, Bank0(cartridge));
}

TEST(Mbc5Test, SelectsRomBankBit8In3000To3fff)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(512), 0 };
	cartridge.RomWriteByte(0x2000, 0x23);

	cartridge.RomWriteByte(0x3000, 0x01);
	EXPECT_EQ(0x123, Bank1(cartridge));

	cartridge.RomWriteByte(0x3fff, 0xfe);
	EXPECT_EQ(0x023, Bank1(cartridge));

	cartridge.RomWriteByte(0x3fff, 0xff);
	cartridge.RomWriteByte(0x2000, 0x00);
	EXPECT_EQ(0x100, Bank1(cartridge));
}

TEST(Mbc5Test, RomBank0CanBeSelected)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 0 };

	cartridge.RomWriteByte(0x2000, 0x00);
	EXPECT_EQ(0, Bank1(cartridge));
}

TEST(Mbc5Test, RomBanksWrapToRomSize)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(64), 0 };

	cartridge.RomWriteByte(0x2000, 0x45);
	cartridge.RomWriteByte(0x3000, 0x01);
	EXPECT_EQ(0x05, Bank1(cartridge));
}

TEST(Mbc5Test, RamEnabledOnlyBy0a)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), RamBankSize };
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RamWriteByte(0x0000, 0x12);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x1fff, 0x1a);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
	cartridge.RamWriteByte(0x0000, 0x34);

	cartridge.RomWriteByte(0x1fff, 0x0a);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
}

TEST(Mbc5Test, SelectsSixteenRamBanks)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 16 * RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);

	for (auto bank = 0; bank < 16; bank++)
	{
		cartridge.RomWriteByte(0x4000, bank);
		cartridge.RamWriteByte(0x1fff, 0x20 + bank);
	}

	for (auto bank = 0; bank < 16; bank++)
	{
		cartridge.RomWriteByte(0x5fff, 0xf0 | bank);
		EXPECT_EQ(0x20 + bank, cartridge.RamReadByte(0x1fff));
	}
}

TEST(Mbc5Test, RamBanksWrapToRamSize)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 4 * RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RomWriteByte(0x4000, 0x01);
	cartridge.RamWriteByte(0x0000, 0x12);

	cartridge.RomWriteByte(0x4000, 0x05);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
}

TEST(Mbc5Test, RumbleBitDrivesMotorNotRamBank)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 16 * RamBankSize, false, true };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RamWriteByte(0x0000, 0x12);
	EXPECT_FALSE(cartridge.IsRumbling());

	cartridge.RomWriteByte(0x4000, 0x08);
	EXPECT_TRUE(cartridge.IsRumbling());
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x4000, 0x00);
	EXPECT_FALSE(cartridge.IsRumbling());
}

TEST(Mbc5Test, WithoutRumbleBit3SelectsRamBank)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 16 * RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RamWriteByte(0x0000, 0x12);

	cartridge.RomWriteByte(0x4000, 0x08);
	EXPECT_FALSE(cartridge.IsRumbling());
	EXPECT_EQ(0x00, cartridge.RamReadByte(0x0000));
}
