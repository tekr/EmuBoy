#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/CartridgeFactory.h"

namespace
{
	std::vector<unsigned char> RomWithRamSize(unsigned char ramSize)
	{
		auto rom = std::vector<unsigned char>(0x8000);
		rom[0x149] = ramSize;
		return rom;
	}

	void EnableRam(Cartridge& cartridge)
	{
		cartridge.RomWriteByte(0x0000, 0x0a);
	}
}

TEST(CartridgeRamTest, RamBanksFromHeader)
{
	EXPECT_EQ(0, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(0)));
	EXPECT_EQ(1, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(1)));
	EXPECT_EQ(1, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(2)));
	EXPECT_EQ(4, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(3)));
	EXPECT_EQ(16, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(4)));
	EXPECT_EQ(8, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(5)));
}

TEST(CartridgeRamTest, RamBanksFromInvalidHeaderIsZero)
{
	EXPECT_EQ(0, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(6)));
	EXPECT_EQ(0, CartridgeFactory::RamBanksFromHeader(RomWithRamSize(0xff)));
	EXPECT_EQ(0, CartridgeFactory::RamBanksFromHeader(std::vector<unsigned char>(0x100)));
}

TEST(CartridgeRamTest, CartridgeWithoutRamReadsFfAndIgnoresWrites)
{
	Cartridge cartridge{ RomWithRamSize(0), 0 };
	EnableRam(cartridge);

	cartridge.RamWriteByte(0x0000, 0x12);
	cartridge.RamWriteByte(0x1fff, 0x34);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x1fff));
}

TEST(CartridgeRamTest, RamKeepsWrittenValues)
{
	Cartridge cartridge{ RomWithRamSize(2), 1 };
	EnableRam(cartridge);

	cartridge.RamWriteByte(0x0000, 0x12);
	cartridge.RamWriteByte(0x1fff, 0x34);

	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0x34, cartridge.RamReadByte(0x1fff));
}

TEST(CartridgeRamTest, BankBeyondCartridgeRamReadsFfAndIgnoresWrites)
{
	Cartridge cartridge{ RomWithRamSize(2), 1 };
	EnableRam(cartridge);
	cartridge.RamWriteByte(0x0000, 0x12);

	// Select RAM bank 3 of a cartridge with only one bank
	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x03);
	cartridge.RamWriteByte(0x0000, 0x56);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x4000, 0x00);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
}
