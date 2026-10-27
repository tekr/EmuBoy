#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/CartridgeFactory.h"
#include "../core/Mbc1Cartridge.h"
#include "../core/Mbc5Cartridge.h"
#include <cstdio>
#include <fstream>
#include <string>

namespace
{
	std::vector<unsigned char> RomWithRamSizeCode(unsigned char sizeCode)
	{
		const unsigned char mbc1RamBattery = 0x03;

		auto rom = std::vector<unsigned char>(0x8000);
		rom[CartridgeFactory::CartridgeTypeHeaderOffset] = mbc1RamBattery;
		rom[CartridgeFactory::RamSizeHeaderOffset] = sizeCode;
		return rom;
	}

	size_t RamSizeForSizeCode(unsigned char sizeCode)
	{
		return CartridgeFactory::RamSizeFromHeader(RomWithRamSizeCode(sizeCode));
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

	// False if the bank mirrors bank 0, as it does when it is beyond the RAM's size
	bool RamBankIsDistinctFromBank0(Cartridge& cartridge, unsigned char bank)
	{
		EnableRam(cartridge);
		SelectRamBank(cartridge, 0);
		cartridge.RamWriteByte(0x0000, 0x12);
		SelectRamBank(cartridge, bank);
		cartridge.RamWriteByte(0x0000, 0x56);
		SelectRamBank(cartridge, 0);
		return cartridge.RamReadByte(0x0000) == 0x12;
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

TEST(CartridgeRamSizeTest, RamSizeFromHeader)
{
	// Pan Docs' RAM size table, with code 1 as the 2 KB that BGB, SameBoy and VBA-M give it
	EXPECT_EQ(0u, RamSizeForSizeCode(0));
	EXPECT_EQ(2048u, RamSizeForSizeCode(1));
	EXPECT_EQ(8192u, RamSizeForSizeCode(2));
	EXPECT_EQ(32768u, RamSizeForSizeCode(3));
	EXPECT_EQ(131072u, RamSizeForSizeCode(4));
	EXPECT_EQ(65536u, RamSizeForSizeCode(5));
}

TEST(CartridgeRamSizeTest, RamSizeFromUnknownOrMissingHeaderIsZero)
{
	EXPECT_EQ(0u, RamSizeForSizeCode(6));
	EXPECT_EQ(0u, RamSizeForSizeCode(0xff));
	EXPECT_EQ(0u, CartridgeFactory::RamSizeFromHeader(std::vector<unsigned char>(0x100)));
}

TEST(CartridgeRamSizeTest, TwoKbRamMirrorsThroughA000ToBfff)
{
	// Only 11 address lines reach a 2 KB chip, so it repeats every $800 bytes and the bank number is ignored
	auto cartridge = CartridgeFactory::Create(RomWithRamSizeCode(1));
	ASSERT_EQ(2048u, cartridge->GetRam().size());
	EnableRam(*cartridge);

	cartridge->RamWriteByte(0x0000, 0x12);
	cartridge->RamWriteByte(0x17ff, 0x34);

	EXPECT_EQ(0x12, cartridge->RamReadByte(0x0800));
	EXPECT_EQ(0x12, cartridge->RamReadByte(0x1000));
	EXPECT_EQ(0x12, cartridge->RamReadByte(0x1800));
	EXPECT_EQ(0x34, cartridge->RamReadByte(0x07ff));
	EXPECT_EQ(0x34, cartridge->RamReadByte(0x1fff));
	EXPECT_EQ(0x12, cartridge->GetRam()[0x0000]);
	EXPECT_EQ(0x34, cartridge->GetRam()[0x07ff]);

	SelectRamBank(*cartridge, 1);
	EXPECT_EQ(0x12, cartridge->RamReadByte(0x0000));
}

TEST(CartridgeRamSizeTest, CartridgeWithoutRamReadsFfAndIgnoresWrites)
{
	Mbc1Cartridge cartridge{ RomWithRamSizeCode(0), 0 };
	EnableRam(cartridge);

	cartridge.RamWriteByte(0x0000, 0x12);
	cartridge.RamWriteByte(0x1fff, 0x34);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x1fff));
}

TEST(CartridgeRamSizeTest, RamKeepsWrittenValues)
{
	Mbc1Cartridge cartridge{ RomWithRamSizeCode(2), Cartridge::RamBankSize };
	EnableRam(cartridge);

	cartridge.RamWriteByte(0x0000, 0x12);
	cartridge.RamWriteByte(0x1fff, 0x34);

	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(0x34, cartridge.RamReadByte(0x1fff));
}

TEST(CartridgeRamSizeTest, BankBeyondCartridgeRamMirrorsLowerBanks)
{
	// The RAM chip's address lines stop at its size
	Mbc1Cartridge cartridge{ RomWithRamSizeCode(2), Cartridge::RamBankSize };

	EXPECT_FALSE(RamBankIsDistinctFromBank0(cartridge, 3));
}

TEST(CartridgeRamSizeTest, RamBanksAreStoredInOrder)
{
	// Bank n is at n * 8 KB in the RAM contents, the layout other emulators' save files use
	Mbc5Cartridge cartridge{ RomWithRamSizeCode(3), 4 * Cartridge::RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);

	cartridge.RomWriteByte(0x4000, 0x02);
	cartridge.RamWriteByte(0x1fff, 0x12);
	cartridge.RomWriteByte(0x4000, 0x07);
	cartridge.RamWriteByte(0x0001, 0x34);

	EXPECT_EQ(0x12, cartridge.GetRam()[2 * Cartridge::RamBankSize + 0x1fff]);
	EXPECT_EQ(0x34, cartridge.GetRam()[3 * Cartridge::RamBankSize + 0x0001]);
}

TEST(CartridgeRamSizeTest, LoadFromFileSizesRamFromHeader)
{
	ASSERT_TRUE(WriteRomFile(RomWithRamSizeCode(3)));
	auto cartridge = CartridgeFactory::LoadFromFile(RomFilePath());
	std::remove(RomFilePath().c_str());

	ASSERT_NE(nullptr, cartridge);
	EXPECT_EQ(32768u, cartridge->GetRam().size());
	EXPECT_TRUE(RamBankIsDistinctFromBank0(*cartridge, 3));
}

TEST(CartridgeRamSizeTest, LoadFromFileWithRamSizeIgnoresHeader)
{
	ASSERT_TRUE(WriteRomFile(RomWithRamSizeCode(3)));
	auto cartridge = CartridgeFactory::LoadFromFile(RomFilePath(), Cartridge::RamBankSize);
	std::remove(RomFilePath().c_str());

	ASSERT_NE(nullptr, cartridge);
	EXPECT_FALSE(RamBankIsDistinctFromBank0(*cartridge, 3));
}

TEST(CartridgeRamSizeTest, LoadFromMissingFileIsNull)
{
	EXPECT_EQ(nullptr, CartridgeFactory::LoadFromFile("missing.gb"));
	EXPECT_EQ(nullptr, CartridgeFactory::LoadFromFile("missing.gb", Cartridge::RamBankSize));
}
