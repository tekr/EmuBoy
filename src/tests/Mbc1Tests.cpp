#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/Mbc1Cartridge.h"
#include "CartridgeTestHelpers.h"
#include <algorithm>

using namespace CartridgeTestHelpers;

// Expectations from https://gbdev.io/pandocs/MBC1.html

namespace
{
	const size_t RamBankSize = Cartridge::RamBankSize;

	std::vector<unsigned char> MulticartRom()
	{
		// Four 256 KB games, each with the same boot logo
		auto rom = BankNumberedRom(64);
		const unsigned char logo[] = { 0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b };

		for (size_t game = 0; game < 4; game++)
		{
			std::fill(rom.begin() + game * 0x40000 + 0x104, rom.begin() + game * 0x40000 + 0x134, 0);
			std::copy(std::begin(logo), std::end(logo), rom.begin() + game * 0x40000 + 0x104);
		}

		return rom;
	}
}

TEST(Mbc1Test, StartsWithBanks0And1)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(4), 0 };

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0, cartridge.RomReadByte(0x3fff));
	EXPECT_EQ(1, cartridge.RomReadByte(0x7fff));
}

TEST(Mbc1Test, SelectsRomBankAnywhereIn2000To3fff)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(32), 0 };

	cartridge.RomWriteByte(0x2000, 0x05);
	EXPECT_EQ(5, Bank1(cartridge));

	cartridge.RomWriteByte(0x3fff, 0x1f);
	EXPECT_EQ(0x1f, Bank1(cartridge));
	EXPECT_EQ(0, Bank0(cartridge));
}

TEST(Mbc1Test, RomBank0SelectsBank1)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(32), 0 };
	cartridge.RomWriteByte(0x2000, 0x05);

	cartridge.RomWriteByte(0x2000, 0x00);
	EXPECT_EQ(1, Bank1(cartridge));

	// Only the lower five bits are stored, so this is bank 0 too
	cartridge.RomWriteByte(0x2000, 0xe0);
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc1Test, Bank0TranslationIgnoresRomSize)
{
	// On a 256 KB ROM, $10 isn't translated (it isn't 0) but bit 4 isn't connected, so bank 0 is mapped
	Mbc1Cartridge cartridge{ BankNumberedRom(16), 0 };

	cartridge.RomWriteByte(0x2000, 0x10);
	EXPECT_EQ(0, Bank1(cartridge));

	cartridge.RomWriteByte(0x2000, 0x11);
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc1Test, RomBanksWrapToRomSize)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(4), 0 };

	cartridge.RomWriteByte(0x2000, 0x06);
	EXPECT_EQ(2, Bank1(cartridge));

	cartridge.RomWriteByte(0x4000, 0x03);
	EXPECT_EQ(2, Bank1(cartridge));
}

TEST(Mbc1Test, Bank2SuppliesUpperRomBankBits)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(128), 0 };

	cartridge.RomWriteByte(0x2000, 0x05);
	cartridge.RomWriteByte(0x4000, 0x02);
	EXPECT_EQ(0x45, Bank1(cartridge));

	cartridge.RomWriteByte(0x5fff, 0xff);
	EXPECT_EQ(0x65, Bank1(cartridge));
}

TEST(Mbc1Test, Banks20_40_60AreReplacedByTheNextBank)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(128), 0 };
	cartridge.RomWriteByte(0x2000, 0x00);

	for (auto bank2 = 1; bank2 < 4; bank2++)
	{
		cartridge.RomWriteByte(0x4000, bank2);
		EXPECT_EQ(bank2 << 5 | 1, Bank1(cartridge));
	}
}

TEST(Mbc1Test, Mode0MapsBank0AtStart)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(128), 0 };
	cartridge.RomWriteByte(0x4000, 0x03);

	EXPECT_EQ(0, Bank0(cartridge));
}

TEST(Mbc1Test, Mode1AppliesBank2ToStartOfRom)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(128), 0 };
	cartridge.RomWriteByte(0x6000, 0x01);

	for (auto bank2 = 0; bank2 < 4; bank2++)
	{
		cartridge.RomWriteByte(0x4000, bank2);
		EXPECT_EQ(bank2 << 5, Bank0(cartridge));
		EXPECT_EQ(bank2 << 5 | 1, Bank1(cartridge));
	}

	cartridge.RomWriteByte(0x7fff, 0x00);
	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(0x61, Bank1(cartridge));
}

TEST(Mbc1Test, Mode1StartOfRomWrapsToRomSize)
{
	// A 512 KB ROM has no address lines for BANK2
	Mbc1Cartridge cartridge{ BankNumberedRom(32), 0 };
	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x01);

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc1Test, RamDisabledAtStartAndReadsFf)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(2), RamBankSize };

	cartridge.RamWriteByte(0x0000, 0x12);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x0000, 0x0a);
	EXPECT_EQ(0x00, cartridge.RamReadByte(0x0000));
}

TEST(Mbc1Test, RamEnabledByLowNibbleAAnywhereIn0000To1fff)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(2), RamBankSize };

	cartridge.RomWriteByte(0x1fff, 0x3a);
	cartridge.RamWriteByte(0x1fff, 0x12);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x1fff));

	cartridge.RomWriteByte(0x0000, 0x0b);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x1fff));
}

TEST(Mbc1Test, RamBankSelectedOnlyInMode1)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(2), 4 * RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);

	for (auto bank = 0; bank < 4; bank++)
	{
		cartridge.RomWriteByte(0x6000, 0x01);
		cartridge.RomWriteByte(0x4000, bank);
		cartridge.RamWriteByte(0x0100, 0x10 + bank);
	}

	for (auto bank = 0; bank < 4; bank++)
	{
		cartridge.RomWriteByte(0x4000, bank);
		EXPECT_EQ(0x10 + bank, cartridge.RamReadByte(0x0100));
	}

	cartridge.RomWriteByte(0x6000, 0x00);
	EXPECT_EQ(0x10, cartridge.RamReadByte(0x0100));
}

TEST(Mbc1Test, SmallRamIgnoresBank2)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(2), RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RamWriteByte(0x0000, 0x12);

	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x02);

	EXPECT_EQ(0x12, cartridge.RamReadByte(0x0000));
}

TEST(Mbc1Test, NoRamReadsFf)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(2), 0 };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RamWriteByte(0x0000, 0x12);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
}

TEST(Mbc1Test, DetectsMulticartByRepeatedLogo)
{
	EXPECT_TRUE(Mbc1Cartridge::IsMulticart(MulticartRom()));
	EXPECT_FALSE(Mbc1Cartridge::IsMulticart(BankNumberedRom(64)));

	auto halfSize = MulticartRom();
	halfSize.resize(halfSize.size() / 2);
	EXPECT_FALSE(Mbc1Cartridge::IsMulticart(halfSize));
}

TEST(Mbc1Test, MulticartUsesFourBitBank1)
{
	Mbc1Cartridge cartridge{ MulticartRom(), 0, false, true };

	cartridge.RomWriteByte(0x2000, 0x02);
	cartridge.RomWriteByte(0x4000, 0x01);
	EXPECT_EQ(0x12, Bank1(cartridge));

	// Bit 4 is still used for the bank 0 translation but not connected
	cartridge.RomWriteByte(0x2000, 0x10);
	EXPECT_EQ(0x10, Bank1(cartridge));

	cartridge.RomWriteByte(0x2000, 0x00);
	EXPECT_EQ(0x11, Bank1(cartridge));
}

TEST(Mbc1Test, MulticartMode1SelectsEachGamesFirstBank)
{
	Mbc1Cartridge cartridge{ MulticartRom(), 0, false, true };
	cartridge.RomWriteByte(0x6000, 0x01);

	for (auto game = 0; game < 4; game++)
	{
		cartridge.RomWriteByte(0x4000, game);
		EXPECT_EQ(game << 4, Bank0(cartridge));
	}
}
