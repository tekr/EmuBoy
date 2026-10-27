#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/RomOnlyCartridge.h"
#include "../core/Mbc1Cartridge.h"
#include "../core/Mbc2Cartridge.h"
#include "../core/Mbc3Cartridge.h"
#include "../core/Mbc5Cartridge.h"
#include "CartridgeTestHelpers.h"

using namespace CartridgeTestHelpers;

// A reset is a power cycle: the mapper's registers return to their power-on state (ROM banks 0 and 1, RAM disabled,
// per https://gbdev.io/pandocs/MBCs.html), while battery-backed RAM and the MBC3's clock keep their contents

namespace
{
	const size_t RamBankSize = Cartridge::RamBankSize;

	void EnableRam(Cartridge& cartridge)
	{
		cartridge.RomWriteByte(0x0000, 0x0a);
	}

	// Writes a byte to cartridge RAM, enabling it first
	void WriteRam(Cartridge& cartridge, unsigned short address, unsigned char value)
	{
		EnableRam(cartridge);
		cartridge.RamWriteByte(address, value);
	}
}

TEST(CartridgeResetTest, RomOnlyKeepsRam)
{
	RomOnlyCartridge cartridge{ BankNumberedRom(2), RamBankSize, true };
	cartridge.RamWriteByte(0x0000, 0x42);

	cartridge.Reset();

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0x42, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeResetTest, Mbc1ReturnsToBank1WithRamDisabled)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(128), 4 * RamBankSize, true };
	cartridge.RomWriteByte(0x2000, 0x05);
	cartridge.RomWriteByte(0x4000, 0x02);
	cartridge.RomWriteByte(0x6000, 0x01);
	WriteRam(cartridge, 0x0000, 0x42);

	cartridge.Reset();

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeResetTest, Mbc1ReturnsToMode0)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(128), 4 * RamBankSize, true };
	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x02);

	cartridge.Reset();

	// In mode 0, BANK2 only affects $4000-$7FFF, so after a reset setting it maps bank $41 there and 0 at $0000
	cartridge.RomWriteByte(0x4000, 0x02);
	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(0x41, Bank1(cartridge));
}

TEST(CartridgeResetTest, Mbc1KeepsRamContents)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(4), 4 * RamBankSize, true };
	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x02);
	WriteRam(cartridge, 0x0010, 0x42);

	cartridge.Reset();

	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x02);
	EnableRam(cartridge);
	EXPECT_EQ(0x42, cartridge.RamReadByte(0x0010));
}

TEST(CartridgeResetTest, Mbc1MulticartReturnsToFirstGame)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(64), 0, false, true };
	cartridge.RomWriteByte(0x6000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x01);

	cartridge.Reset();

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(CartridgeResetTest, Mbc2ReturnsToBank1WithRamDisabledAndKeepsRam)
{
	Mbc2Cartridge cartridge{ BankNumberedRom(16), true };
	cartridge.RomWriteByte(0x0100, 0x07);
	WriteRam(cartridge, 0x0000, 0x05);

	cartridge.Reset();

	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	EnableRam(cartridge);
	EXPECT_EQ(0xf5, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeResetTest, Mbc3ReturnsToBank1AndRamBank0WithRamDisabled)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(16), 4 * RamBankSize, true };
	WriteRam(cartridge, 0x0000, 0x11);
	cartridge.RomWriteByte(0x2000, 0x07);
	cartridge.RomWriteByte(0x4000, 0x02);
	cartridge.RamWriteByte(0x0000, 0x22);

	cartridge.Reset();

	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	EnableRam(cartridge);
	EXPECT_EQ(0x11, cartridge.RamReadByte(0x0000));
	cartridge.RomWriteByte(0x4000, 0x02);
	EXPECT_EQ(0x22, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeResetTest, Mbc3ClockKeepsCounting)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(4), RamBankSize, true, true };
	cartridge.GetRealTimeClock()->AdvanceSeconds(30);

	cartridge.Reset();
	cartridge.RunCycles(RealTimeClock::CyclesPerSecond);

	EXPECT_EQ(31, cartridge.GetRealTimeClock()->GetState().Counters[RealTimeClock::Seconds]);
}

TEST(CartridgeResetTest, Mbc3ResetDoesntSelectClockRegister)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(4), RamBankSize, true, true };
	WriteRam(cartridge, 0x0000, 0x33);
	cartridge.RomWriteByte(0x4000, 0x08);

	cartridge.Reset();

	EnableRam(cartridge);
	EXPECT_EQ(0x33, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeResetTest, Mbc5ReturnsToBank1AndRamBank0WithRamDisabled)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(512), 4 * RamBankSize, true };
	WriteRam(cartridge, 0x0000, 0x11);
	cartridge.RomWriteByte(0x2000, 0x34);
	cartridge.RomWriteByte(0x3000, 0x01);
	cartridge.RomWriteByte(0x4000, 0x03);
	cartridge.RamWriteByte(0x0000, 0x22);

	cartridge.Reset();

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	EnableRam(cartridge);
	EXPECT_EQ(0x11, cartridge.RamReadByte(0x0000));
	cartridge.RomWriteByte(0x4000, 0x03);
	EXPECT_EQ(0x22, cartridge.RamReadByte(0x0000));
}

TEST(CartridgeResetTest, Mbc5ResetKeepsRomBankHighBitClear)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(512), 0 };
	cartridge.RomWriteByte(0x3000, 0x01);

	cartridge.Reset();

	// Writing only the low byte after a reset gives a bank below $100, as the high bit is back to 0
	cartridge.RomWriteByte(0x2000, 0x05);
	EXPECT_EQ(5, Bank1(cartridge));
}

TEST(CartridgeResetTest, Mbc5StopsRumbling)
{
	Mbc5Cartridge cartridge{ BankNumberedRom(4), 0, false, true };
	cartridge.RomWriteByte(0x4000, 0x08);

	cartridge.Reset();

	EXPECT_FALSE(cartridge.IsRumbling());
}

TEST(CartridgeResetTest, KeepsUnsavedChangesFlag)
{
	Mbc1Cartridge cartridge{ BankNumberedRom(4), RamBankSize, true };
	WriteRam(cartridge, 0x0000, 0x42);

	cartridge.Reset();

	EXPECT_TRUE(cartridge.HasUnsavedChanges());
}
