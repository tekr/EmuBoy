#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/CartridgeFactory.h"

namespace
{
	std::vector<unsigned char> RomWithRamSizeCode(unsigned char sizeCode)
	{
		auto rom = std::vector<unsigned char>(0x8000);
		rom[CartridgeFactory::RamSizeHeaderOffset] = sizeCode;
		return rom;
	}

	int RamBanksForSizeCode(unsigned char sizeCode)
	{
		return CartridgeFactory::RamBanksFromHeader(RomWithRamSizeCode(sizeCode));
	}

	void EnableRam(Cartridge& cartridge)
	{
		cartridge.RomWriteByte(0x0000, 0x0a);
	}

	void SelectRamBank(Cartridge& cartridge, unsigned char bank)
	{
		// MBC1 only switches RAM banks in its 4 Mb ROM mode
		cartridge.RomWriteByte(0x6000, 0x01);
		cartridge.RomWriteByte(0x4000, bank);
	}
}

TEST(CartridgeRamSizeTest, RamBanksFromHeader)
{
	EXPECT_EQ(0, RamBanksForSizeCode(0));
	EXPECT_EQ(1, RamBanksForSizeCode(1));
	EXPECT_EQ(1, RamBanksForSizeCode(2));
	EXPECT_EQ(4, RamBanksForSizeCode(3));
	EXPECT_EQ(16, RamBanksForSizeCode(4));
	EXPECT_EQ(8, RamBanksForSizeCode(5));
}

TEST(CartridgeRamSizeTest, RamBanksFromUnknownOrMissingHeaderIsZero)
{
	EXPECT_EQ(0, RamBanksForSizeCode(6));
	EXPECT_EQ(0, RamBanksForSizeCode(0xff));
	EXPECT_EQ(0, CartridgeFactory::RamBanksFromHeader(std::vector<unsigned char>(0x100)));
}

TEST(CartridgeRamSizeTest, CartridgeWithoutRamReadsFfAndIgnoresWrites)
{
	Cartridge cartridge{ RomWithRamSizeCode(0), 0 };
	EnableRam(cartridge);

	cartridge.RamWriteByte(0x0000, 0x12);
	cartridge.RamWriteByte(0x1fff, 0x34);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x1fff));
}

TEST(CartridgeRamSizeTest, RamKeepsWrittenValues)
{
	Cartridge cartridge{ RomWithRamSizeCode(2), 1 };
	EnableRam(cartridge);

	cartridge.RamWriteByte(0x0000, 0x12);
	cartridge.RamWriteByte(0x1fff, 0x34);

	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0x34, cartridge.RamReadByte(0x1fff));
}

TEST(CartridgeRamSizeTest, BankBeyondCartridgeRamReadsFfAndIgnoresWrites)
{
	Cartridge cartridge{ RomWithRamSizeCode(2), 1 };
	EnableRam(cartridge);
	cartridge.RamWriteByte(0x0000, 0x12);

	SelectRamBank(cartridge, 3);
	cartridge.RamWriteByte(0x0000, 0x56);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	SelectRamBank(cartridge, 0);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
}
