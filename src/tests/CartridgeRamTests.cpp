#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/CartridgeFactory.h"
#include <cstdio>
#include <fstream>
#include <string>

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

	bool RamBankKeepsWrittenValue(Cartridge& cartridge, unsigned char bank)
	{
		EnableRam(cartridge);
		SelectRamBank(cartridge, bank);
		cartridge.RamWriteByte(0x0000, 0x56);
		return cartridge.RamReadByte(0x0000) == 0x56;
	}

	// Named after the test, so test processes sharing a directory don't overwrite each other's file
	std::string RomFilePath()
	{
		return std::string(testing::UnitTest::GetInstance()->current_test_info()->name()) + ".gb";
	}

	bool WriteRomFile(const std::vector<unsigned char>& rom)
	{
		std::ofstream ofs(RomFilePath(), std::ios::binary);
		ofs.write(reinterpret_cast<const char*>(rom.data()), rom.size());
		return static_cast<bool>(ofs);
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

TEST(CartridgeRamSizeTest, LoadFromFileSizesRamFromHeader)
{
	ASSERT_TRUE(WriteRomFile(RomWithRamSizeCode(3)));
	auto cartridge = CartridgeFactory::LoadFromFile(RomFilePath());
	std::remove(RomFilePath().c_str());

	ASSERT_NE(nullptr, cartridge);
	EXPECT_TRUE(RamBankKeepsWrittenValue(*cartridge, 3));
}

TEST(CartridgeRamSizeTest, LoadFromFileWithRamBanksIgnoresHeader)
{
	ASSERT_TRUE(WriteRomFile(RomWithRamSizeCode(3)));
	auto cartridge = CartridgeFactory::LoadFromFile(RomFilePath(), 1);
	std::remove(RomFilePath().c_str());

	ASSERT_NE(nullptr, cartridge);
	EXPECT_FALSE(RamBankKeepsWrittenValue(*cartridge, 3));
}

TEST(CartridgeRamSizeTest, LoadFromMissingFileIsNull)
{
	EXPECT_EQ(nullptr, CartridgeFactory::LoadFromFile("missing.gb"));
	EXPECT_EQ(nullptr, CartridgeFactory::LoadFromFile("missing.gb", 1));
}
