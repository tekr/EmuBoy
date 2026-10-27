#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/CartridgeFactory.h"
#include "../core/RomOnlyCartridge.h"
#include "../core/Mbc1Cartridge.h"
#include "../core/Mbc2Cartridge.h"
#include "../core/Mbc3Cartridge.h"
#include "../core/Mbc5Cartridge.h"
#include "CartridgeTestHelpers.h"

using namespace CartridgeTestHelpers;
using MapperType = CartridgeFactory::MapperType;

// Cartridge type codes from https://gbdev.io/pandocs/The_Cartridge_Header.html#0147--cartridge-type

namespace
{
	std::vector<unsigned char> RomWithHeader(unsigned char typeCode, unsigned char ramSizeCode = 0, int banks = 4)
	{
		auto rom = BankNumberedRom(banks);
		rom[CartridgeFactory::CartridgeTypeHeaderOffset] = typeCode;
		rom[CartridgeFactory::RamSizeHeaderOffset] = ramSizeCode;
		return rom;
	}

	CartridgeFactory::CartridgeType TypeOf(unsigned char typeCode)
	{
		return CartridgeFactory::TypeFromHeader(RomWithHeader(typeCode));
	}

	struct ExpectedType
	{
		unsigned char Code;
		MapperType Mapper;
		bool HasBattery;
		bool HasClock;
		bool HasRumble;
	};
}

TEST(CartridgeFactoryTest, MapsSupportedCartridgeTypes)
{
	const ExpectedType types[] =
	{
		{ 0x00, MapperType::RomOnly, false, false, false },
		{ 0x01, MapperType::Mbc1, false, false, false },
		{ 0x02, MapperType::Mbc1, false, false, false },
		{ 0x03, MapperType::Mbc1, true, false, false },
		{ 0x05, MapperType::Mbc2, false, false, false },
		{ 0x06, MapperType::Mbc2, true, false, false },
		{ 0x08, MapperType::RomOnly, false, false, false },
		{ 0x09, MapperType::RomOnly, true, false, false },
		{ 0x0f, MapperType::Mbc3, true, true, false },
		{ 0x10, MapperType::Mbc3, true, true, false },
		{ 0x11, MapperType::Mbc3, false, false, false },
		{ 0x12, MapperType::Mbc3, false, false, false },
		{ 0x13, MapperType::Mbc3, true, false, false },
		{ 0x19, MapperType::Mbc5, false, false, false },
		{ 0x1a, MapperType::Mbc5, false, false, false },
		{ 0x1b, MapperType::Mbc5, true, false, false },
		{ 0x1c, MapperType::Mbc5, false, false, true },
		{ 0x1d, MapperType::Mbc5, false, false, true },
		{ 0x1e, MapperType::Mbc5, true, false, true },
	};

	for (auto& expected : types)
	{
		SCOPED_TRACE(static_cast<int>(expected.Code));
		auto type = TypeOf(expected.Code);

		EXPECT_EQ(expected.Mapper, type.Mapper);
		EXPECT_EQ(expected.HasBattery, type.HasBattery);
		EXPECT_EQ(expected.HasClock, type.HasClock);
		EXPECT_EQ(expected.HasRumble, type.HasRumble);
		EXPECT_TRUE(type.IsSupported);
	}
}

TEST(CartridgeFactoryTest, UnsupportedTypesFallBackToMbc1)
{
	// Unassigned codes, MMM01, MMM01+RAM, MMM01+RAM+BATTERY, MBC6, MBC7+SENSOR+RUMBLE+RAM+BATTERY, Pocket Camera,
	// TAMA5, HuC3 (battery-backed RAM and a clock) and HuC1+RAM+BATTERY
	const ExpectedType types[] =
	{
		{ 0x04, MapperType::Mbc1, false, false, false },
		{ 0x0b, MapperType::Mbc1, false, false, false },
		{ 0x0c, MapperType::Mbc1, false, false, false },
		{ 0x0d, MapperType::Mbc1, true, false, false },
		{ 0x20, MapperType::Mbc1, false, false, false },
		{ 0x22, MapperType::Mbc1, true, false, true },
		{ 0xfc, MapperType::Mbc1, true, false, false },
		{ 0xfd, MapperType::Mbc1, false, false, false },
		{ 0xfe, MapperType::Mbc1, true, true, false },
		{ 0xff, MapperType::Mbc1, true, false, false },
	};

	for (auto& expected : types)
	{
		SCOPED_TRACE(static_cast<int>(expected.Code));
		auto type = TypeOf(expected.Code);

		EXPECT_EQ(expected.Mapper, type.Mapper);
		EXPECT_EQ(expected.HasBattery, type.HasBattery);
		EXPECT_EQ(expected.HasClock, type.HasClock);
		EXPECT_EQ(expected.HasRumble, type.HasRumble);
		EXPECT_FALSE(type.IsSupported);
		EXPECT_NE(nullptr, dynamic_cast<Mbc1Cartridge*>(CartridgeFactory::Create(RomWithHeader(expected.Code)).get()));
	}
}

TEST(CartridgeFactoryTest, UnsupportedTypeKeepsBatteryAndHeaderRamSize)
{
	// HuC1+RAM+BATTERY with 32 KB RAM, and a Pocket Camera with 128 KB, keep their saves while running as MBC1
	auto cartridge = CartridgeFactory::Create(RomWithHeader(0xff, 0x03));
	EXPECT_TRUE(cartridge->HasBattery());
	EXPECT_EQ(32768u, cartridge->GetRam().size());

	cartridge = CartridgeFactory::Create(RomWithHeader(0xfc, 0x04));
	EXPECT_TRUE(cartridge->HasBattery());
	EXPECT_EQ(131072u, cartridge->GetRam().size());

	EXPECT_FALSE(CartridgeFactory::Create(RomWithHeader(0x0c, 0x02))->HasBattery());
}

TEST(CartridgeFactoryTest, RomWithoutHeaderIsRomOnly)
{
	auto rom = std::vector<unsigned char>(0x100);

	EXPECT_EQ(MapperType::RomOnly, CartridgeFactory::TypeFromHeader(rom).Mapper);
	EXPECT_NE(nullptr, dynamic_cast<RomOnlyCartridge*>(CartridgeFactory::Create(std::move(rom)).get()));
}

TEST(CartridgeFactoryTest, CreatesCartridgeForMapper)
{
	EXPECT_NE(nullptr, dynamic_cast<RomOnlyCartridge*>(CartridgeFactory::Create(RomWithHeader(0x00)).get()));
	EXPECT_NE(nullptr, dynamic_cast<Mbc1Cartridge*>(CartridgeFactory::Create(RomWithHeader(0x03)).get()));
	EXPECT_NE(nullptr, dynamic_cast<Mbc2Cartridge*>(CartridgeFactory::Create(RomWithHeader(0x06)).get()));
	EXPECT_NE(nullptr, dynamic_cast<Mbc3Cartridge*>(CartridgeFactory::Create(RomWithHeader(0x10)).get()));
	EXPECT_NE(nullptr, dynamic_cast<Mbc5Cartridge*>(CartridgeFactory::Create(RomWithHeader(0x1e)).get()));
}

TEST(CartridgeFactoryTest, PassesBatteryClockAndRamSize)
{
	auto cartridge = CartridgeFactory::Create(RomWithHeader(0x10, 0x03));

	EXPECT_TRUE(cartridge->HasBattery());
	EXPECT_NE(nullptr, cartridge->GetRealTimeClock());
	const size_t ramBankSize = Cartridge::RamBankSize;
	EXPECT_EQ(4 * ramBankSize, cartridge->GetRam().size());

	cartridge = CartridgeFactory::Create(RomWithHeader(0x13, 0x02));
	EXPECT_TRUE(cartridge->HasBattery());
	EXPECT_EQ(nullptr, cartridge->GetRealTimeClock());
	EXPECT_EQ(ramBankSize, cartridge->GetRam().size());

	EXPECT_FALSE(CartridgeFactory::Create(RomWithHeader(0x01))->HasBattery());
}

TEST(CartridgeFactoryTest, Mbc2HasBuiltInRamWhateverTheHeaderSays)
{
	const size_t ramSize = Mbc2Cartridge::RamSize;

	EXPECT_EQ(ramSize, CartridgeFactory::Create(RomWithHeader(0x06, 0x00))->GetRam().size());
	EXPECT_EQ(ramSize, CartridgeFactory::Create(RomWithHeader(0x06, 0x03))->GetRam().size());
}

TEST(CartridgeFactoryTest, CreatesMbc30ForRamOrRomBeyondMbc3Limits)
{
	// 64 KB RAM (code 5): bank 7 is distinct from bank 3
	auto cartridge = CartridgeFactory::Create(RomWithHeader(0x13, 0x05));
	cartridge->RomWriteByte(0x0000, 0x0a);
	cartridge->RomWriteByte(0x4000, 0x03);
	cartridge->RamWriteByte(0x0000, 0x12);
	cartridge->RomWriteByte(0x4000, 0x07);
	cartridge->RamWriteByte(0x0000, 0x34);
	EXPECT_EQ(0x34, cartridge->RamReadByte(0x0000));
	cartridge->RomWriteByte(0x4000, 0x03);
	EXPECT_EQ(0x12, cartridge->RamReadByte(0x0000));

	// 4 MB ROM: bank $81 is selectable
	cartridge = CartridgeFactory::Create(RomWithHeader(0x10, 0x03, 256));
	cartridge->RomWriteByte(0x2000, 0x81);
	EXPECT_EQ(0x81, Bank1(*cartridge));

	// 2 MB ROM and 32 KB RAM is plain MBC3
	cartridge = CartridgeFactory::Create(RomWithHeader(0x13, 0x03, 128));
	cartridge->RomWriteByte(0x2000, 0x81);
	EXPECT_EQ(0x01, Bank1(*cartridge));
}

TEST(CartridgeFactoryTest, RumbleCartridgeUsesBit3ForMotor)
{
	auto cartridge = CartridgeFactory::Create(RomWithHeader(0x1c, 0x04));
	cartridge->RomWriteByte(0x4000, 0x08);

	EXPECT_TRUE(static_cast<Mbc5Cartridge&>(*cartridge).IsRumbling());
}

TEST(CartridgeFactoryTest, DetectsMbc1Multicart)
{
	// A 1 MB ROM with the boot logo repeated in its second 256 KB game
	auto rom = RomWithHeader(0x01, 0x00, 64);
	std::fill(rom.begin() + 0x104, rom.begin() + 0x134, 0x5a);
	std::fill(rom.begin() + 0x40104, rom.begin() + 0x40134, 0x5a);

	auto cartridge = CartridgeFactory::Create(std::move(rom));
	cartridge->RomWriteByte(0x4000, 0x01);

	EXPECT_EQ(0x11, Bank1(*cartridge));
}

TEST(RomOnlyCartridgeTest, MapsFirst32KbAndIgnoresWrites)
{
	RomOnlyCartridge cartridge{ BankNumberedRom(2), 0 };

	cartridge.RomWriteByte(0x2000, 0x00);
	cartridge.RomWriteByte(0x4000, 0x01);

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(RomOnlyCartridgeTest, RamIsAlwaysEnabled)
{
	RomOnlyCartridge cartridge{ BankNumberedRom(2), Cartridge::RamBankSize, true };

	cartridge.RamWriteByte(0x1fff, 0x12);
	EXPECT_EQ(0x12, cartridge.RamReadByte(0x1fff));
	EXPECT_EQ(0x12, cartridge.GetRam()[0x1fff]);
	EXPECT_TRUE(cartridge.HasBattery());
}

TEST(RomOnlyCartridgeTest, WithoutRamReadsFf)
{
	RomOnlyCartridge cartridge{ BankNumberedRom(2), 0 };

	cartridge.RamWriteByte(0x0000, 0x12);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeTest, RomSmallerThanABankIsPaddedWithFf)
{
	RomOnlyCartridge cartridge{ std::vector<unsigned char>(0x100, 0x12), 0 };

	EXPECT_EQ(0x12, cartridge.RomReadByte(0x00ff));
	EXPECT_EQ(0xff, cartridge.RomReadByte(0x0100));
	EXPECT_EQ(0xff, cartridge.RomReadByte(0x3fff));
}

TEST(CartridgeTest, RomBankCountRoundsUpToPowerOfTwo)
{
	// Three banks are padded to four, so bank 3 is padding and bank 4 wraps to bank 0
	Mbc5Cartridge cartridge{ BankNumberedRom(3), 0 };

	cartridge.RomWriteByte(0x2000, 0x02);
	EXPECT_EQ(2, Bank1(cartridge));

	cartridge.RomWriteByte(0x2000, 0x03);
	EXPECT_EQ(0xff, cartridge.RomReadByte(0x4000));

	cartridge.RomWriteByte(0x2000, 0x04);
	EXPECT_EQ(0, Bank1(cartridge));
}

namespace
{
	// A ROM with the given bytes at the title's place in the header, $0134-$0143
	std::vector<unsigned char> RomWithTitle(const std::vector<unsigned char>& titleBytes)
	{
		std::vector<unsigned char> rom(0x8000);
		std::copy(titleBytes.begin(), titleBytes.end(), rom.begin() + 0x134);
		return rom;
	}

	std::vector<unsigned char> Bytes(const std::string& text)
	{
		return { text.begin(), text.end() };
	}
}

// Title format from https://gbdev.io/pandocs/The_Cartridge_Header.html#0134-0143--title

TEST(CartridgeTitleTest, StopsAtPadding)
{
	EXPECT_EQ("TETRIS", CartridgeFactory::TitleFromHeader(RomWithTitle(Bytes("TETRIS"))));
}

TEST(CartridgeTitleTest, UsesAllSixteenBytes)
{
	EXPECT_EQ("SUPER MARIOLAND!", CartridgeFactory::TitleFromHeader(RomWithTitle(Bytes("SUPER MARIOLAND!"))));
}

TEST(CartridgeTitleTest, DoesntReadPastHeader)
{
	auto rom = RomWithTitle(Bytes("ABCDEFGHIJKLMNOP"));
	rom[0x144] = 'Q';

	EXPECT_EQ("ABCDEFGHIJKLMNOP", CartridgeFactory::TitleFromHeader(rom));
}

TEST(CartridgeTitleTest, StopsAtColourFlag)
{
	auto bytes = Bytes("POKEMON YELLOW");
	bytes.resize(15);
	bytes.push_back(0x80);

	EXPECT_EQ("POKEMON YELLOW", CartridgeFactory::TitleFromHeader(RomWithTitle(bytes)));
}

TEST(CartridgeTitleTest, StopsAtNonPrintableBytes)
{
	EXPECT_EQ("AB", CartridgeFactory::TitleFromHeader(RomWithTitle({ 'A', 'B', 0x0a, 'C' })));
	EXPECT_EQ("AB", CartridgeFactory::TitleFromHeader(RomWithTitle({ 'A', 'B', 0xc3, 'C' })));
	EXPECT_EQ("AB", CartridgeFactory::TitleFromHeader(RomWithTitle({ 'A', 'B', 0x7f, 'C' })));
}

TEST(CartridgeTitleTest, LeavesOutManufacturerCode)
{
	auto bytes = Bytes("POKEMON_GLDAAUE");
	bytes.push_back(0x80);

	EXPECT_EQ("POKEMON_GLD", CartridgeFactory::TitleFromHeader(RomWithTitle(bytes)));
}

TEST(CartridgeTitleTest, KeepsFifteenCharacterColourTitle)
{
	// Not a manufacturer code, as it has a space
	auto bytes = Bytes("SUPER GAME DX 2");
	bytes.push_back(0xc0);

	EXPECT_EQ("SUPER GAME DX 2", CartridgeFactory::TitleFromHeader(RomWithTitle(bytes)));
}

TEST(CartridgeTitleTest, NoManufacturerCodeWithoutColourFlag)
{
	EXPECT_EQ("ABCDEFGHIJKWXYZ", CartridgeFactory::TitleFromHeader(RomWithTitle(Bytes("ABCDEFGHIJKWXYZ"))));
}

TEST(CartridgeTitleTest, TrimsSpaces)
{
	EXPECT_EQ("DR. MARIO", CartridgeFactory::TitleFromHeader(RomWithTitle(Bytes("  DR. MARIO     "))));
}

TEST(CartridgeTitleTest, EmptyWhenBlankOrMissing)
{
	EXPECT_EQ("", CartridgeFactory::TitleFromHeader(RomWithTitle({})));
	EXPECT_EQ("", CartridgeFactory::TitleFromHeader(RomWithTitle(Bytes("    "))));
	EXPECT_EQ("", CartridgeFactory::TitleFromHeader(std::vector<unsigned char>(0x100)));
}

TEST(CartridgeTitleTest, ShortRomGivesWhatItHas)
{
	std::vector<unsigned char> rom(0x137);
	rom[0x134] = 'A';
	rom[0x135] = 'B';
	rom[0x136] = 'C';

	EXPECT_EQ("ABC", CartridgeFactory::TitleFromHeader(rom));
}
