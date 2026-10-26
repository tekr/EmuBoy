#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/Mbc2Cartridge.h"
#include "CartridgeTestHelpers.h"

using namespace CartridgeTestHelpers;

// Expectations from https://gbdev.io/pandocs/MBC2.html

namespace
{
	Mbc2Cartridge CartridgeWithRamEnabled()
	{
		Mbc2Cartridge cartridge{ BankNumberedRom(16) };
		cartridge.RomWriteByte(0x0000, 0x0a);
		return cartridge;
	}
}

TEST(Mbc2Test, StartsWithBanks0And1)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16) };

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc2Test, SelectsRomBankWhenAddressBit8Set)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16) };

	cartridge.RomWriteByte(0x2100, 0x05);
	EXPECT_EQ(5, Bank1(cartridge));

	cartridge.RomWriteByte(0x0100, 0x07);
	EXPECT_EQ(7, Bank1(cartridge));

	cartridge.RomWriteByte(0x3fff, 0x0f);
	EXPECT_EQ(0xf, Bank1(cartridge));
	EXPECT_EQ(0, Bank0(cartridge));
}

TEST(Mbc2Test, IgnoresRomBankWriteWhenAddressBit8Clear)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16) };

	cartridge.RomWriteByte(0x2000, 0x05);
	cartridge.RomWriteByte(0x3eff, 0x05);
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc2Test, RomBankUsesFourBitsAnd0SelectsBank1)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16) };

	cartridge.RomWriteByte(0x2100, 0xf3);
	EXPECT_EQ(3, Bank1(cartridge));

	cartridge.RomWriteByte(0x2100, 0x00);
	EXPECT_EQ(1, Bank1(cartridge));

	cartridge.RomWriteByte(0x2100, 0x10);
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc2Test, RomBanksWrapToRomSize)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(4) };

	cartridge.RomWriteByte(0x2100, 0x06);
	EXPECT_EQ(2, Bank1(cartridge));

	// Bank 4 wraps to bank 0, as the 0 to 1 translation applies to the register value
	cartridge.RomWriteByte(0x2100, 0x04);
	EXPECT_EQ(0, Bank1(cartridge));
}

TEST(Mbc2Test, IgnoresWritesFrom4000)
{
	auto cartridge = CartridgeWithRamEnabled();
	cartridge.RamWriteByte(0x0000, 0x05);

	cartridge.RomWriteByte(0x4100, 0x05);
	cartridge.RomWriteByte(0x6000, 0x00);

	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0xf5, cartridge.RamReadByte(0x0000));
}

TEST(Mbc2Test, RamEnabledWhenAddressBit8Clear)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16) };
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x0100, 0x0a);
	cartridge.RamWriteByte(0x0000, 0x05);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x3eff, 0x1a);
	cartridge.RamWriteByte(0x0000, 0x05);
	EXPECT_EQ(0xf5, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x0000, 0x00);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
}

TEST(Mbc2Test, RamStoresLowerNibbleAndReadsUpperAsOnes)
{
	auto cartridge = CartridgeWithRamEnabled();

	cartridge.RamWriteByte(0x0000, 0x00);
	cartridge.RamWriteByte(0x01ff, 0xa5);

	EXPECT_EQ(0xf0, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0xf5, cartridge.RamReadByte(0x01ff));
}

TEST(Mbc2Test, RamEchoesThroughA000ToBfff)
{
	auto cartridge = CartridgeWithRamEnabled();

	cartridge.RamWriteByte(0x0012, 0x03);
	EXPECT_EQ(0xf3, cartridge.RamReadByte(0x0212));
	EXPECT_EQ(0xf3, cartridge.RamReadByte(0x1e12));

	cartridge.RamWriteByte(0x1fff, 0x04);
	EXPECT_EQ(0xf4, cartridge.RamReadByte(0x01ff));
}

TEST(Mbc2Test, Has512NibblesOfRam)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16), true };

	EXPECT_EQ(512u, cartridge.GetRam().size());
	EXPECT_TRUE(cartridge.HasBattery());
}
