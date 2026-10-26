#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/Mbc3Cartridge.h"
#include "../core/Emulator.h"
#include "../core/Graphics.h"
#include "CartridgeTestHelpers.h"

using namespace CartridgeTestHelpers;

// Expectations from https://gbdev.io/pandocs/MBC3.html

namespace
{
	const size_t RamBankSize = Cartridge::RamBankSize;

	class Mbc3ClockTest : public testing::Test
	{
	public:
		Mbc3Cartridge Cartridge{ BankNumberedRom(4), 4 * RamBankSize, true, true };

		Mbc3ClockTest()
		{
			Cartridge.RomWriteByte(0x0000, 0x0a);
		}

		void Latch()
		{
			Cartridge.RomWriteByte(0x6000, 0x00);
			Cartridge.RomWriteByte(0x7fff, 0x01);
		}

		unsigned char ReadClock(int reg)
		{
			Cartridge.RomWriteByte(0x4000, 0x08 + reg);
			return Cartridge.RamReadByte(0x0000);
		}

		void WriteClock(int reg, unsigned char value)
		{
			Cartridge.RomWriteByte(0x4000, 0x08 + reg);
			Cartridge.RamWriteByte(0x0000, value);
		}
	};
}

TEST(Mbc3Test, StartsWithBanks0And1)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(4), 0 };

	EXPECT_EQ(0, Bank0(cartridge));
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc3Test, SelectsSevenBitRomBank)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(128), 0 };

	cartridge.RomWriteByte(0x2000, 0x45);
	EXPECT_EQ(0x45, Bank1(cartridge));

	cartridge.RomWriteByte(0x3fff, 0xff);
	EXPECT_EQ(0x7f, Bank1(cartridge));
	EXPECT_EQ(0, Bank0(cartridge));
}

TEST(Mbc3Test, RomBank0SelectsBank1)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(128), 0 };
	cartridge.RomWriteByte(0x2000, 0x05);

	cartridge.RomWriteByte(0x2000, 0x00);
	EXPECT_EQ(1, Bank1(cartridge));

	cartridge.RomWriteByte(0x2000, 0x80);
	EXPECT_EQ(1, Bank1(cartridge));
}

TEST(Mbc3Test, RomBanksWrapToRomSize)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(8), 0 };

	cartridge.RomWriteByte(0x2000, 0x0b);
	EXPECT_EQ(3, Bank1(cartridge));
}

TEST(Mbc3Test, RamDisabledAtStartAndReadsFf)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(4), RamBankSize };

	cartridge.RamWriteByte(0x0000, 0x12);
	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));

	cartridge.RomWriteByte(0x1fff, 0x0a);
	EXPECT_EQ(0x00, cartridge.RamReadByte(0x0000));
}

TEST(Mbc3Test, SelectsFourRamBanks)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(4), 4 * RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);

	for (auto bank = 0; bank < 4; bank++)
	{
		cartridge.RomWriteByte(0x4000, bank);
		cartridge.RamWriteByte(0x0abc, 0x30 + bank);
	}

	for (auto bank = 0; bank < 4; bank++)
	{
		cartridge.RomWriteByte(0x5fff, bank);
		EXPECT_EQ(0x30 + bank, cartridge.RamReadByte(0x0abc));
	}
}

TEST(Mbc3Test, WithoutClockClockRegistersReadFf)
{
	Mbc3Cartridge cartridge{ BankNumberedRom(4), 4 * RamBankSize };
	cartridge.RomWriteByte(0x0000, 0x0a);
	cartridge.RomWriteByte(0x4000, 0x08);
	cartridge.RamWriteByte(0x0000, 0x12);

	EXPECT_EQ(0xff, cartridge.RamReadByte(0x0000));
	EXPECT_EQ(nullptr, cartridge.GetRealTimeClock());

	cartridge.RomWriteByte(0x4000, 0x00);
	EXPECT_EQ(0x00, cartridge.RamReadByte(0x0000));
}

TEST_F(Mbc3ClockTest, ExposesClock)
{
	EXPECT_NE(nullptr, Cartridge.GetRealTimeClock());
	EXPECT_TRUE(Cartridge.HasBattery());
}

TEST_F(Mbc3ClockTest, MapsClockRegistersAt08To0c)
{
	for (int reg = RealTimeClock::Seconds; reg <= RealTimeClock::DaysLow; reg++)
	{
		WriteClock(reg, 0x10 + reg);
	}

	WriteClock(RealTimeClock::DaysHigh, 0xc1);

	EXPECT_EQ(0x10, ReadClock(RealTimeClock::Seconds));
	EXPECT_EQ(0x11, ReadClock(RealTimeClock::Minutes));
	EXPECT_EQ(0x12, ReadClock(RealTimeClock::Hours));
	EXPECT_EQ(0x13, ReadClock(RealTimeClock::DaysLow));
	EXPECT_EQ(0xc1, ReadClock(RealTimeClock::DaysHigh));

	// RAM is left alone
	Cartridge.RomWriteByte(0x4000, 0x00);
	EXPECT_EQ(0x00, Cartridge.RamReadByte(0x0000));
}

TEST_F(Mbc3ClockTest, UnmappedRegistersReadFf)
{
	Cartridge.RomWriteByte(0x4000, 0x0d);
	EXPECT_EQ(0xff, Cartridge.RamReadByte(0x0000));

	Cartridge.RomWriteByte(0x4000, 0x04);
	EXPECT_EQ(0xff, Cartridge.RamReadByte(0x0000));
}

TEST_F(Mbc3ClockTest, ClockRegistersReadFfWhenDisabled)
{
	WriteClock(RealTimeClock::Minutes, 5);
	Cartridge.RomWriteByte(0x0000, 0x00);
	WriteClock(RealTimeClock::Minutes, 6);

	EXPECT_EQ(0xff, ReadClock(RealTimeClock::Minutes));

	Cartridge.RomWriteByte(0x0000, 0x0a);
	EXPECT_EQ(5, ReadClock(RealTimeClock::Minutes));
}

TEST_F(Mbc3ClockTest, RunCyclesDrivesClockAndLatchFreezesReads)
{
	Cartridge.RunCycles(2 * RealTimeClock::CyclesPerSecond);
	EXPECT_EQ(0, ReadClock(RealTimeClock::Seconds));

	Latch();
	EXPECT_EQ(2, ReadClock(RealTimeClock::Seconds));

	Cartridge.RunCycles(RealTimeClock::CyclesPerSecond);
	EXPECT_EQ(2, ReadClock(RealTimeClock::Seconds));

	Latch();
	EXPECT_EQ(3, ReadClock(RealTimeClock::Seconds));
}

TEST_F(Mbc3ClockTest, HaltBitStopsClock)
{
	WriteClock(RealTimeClock::DaysHigh, RealTimeClock::HaltBit);
	Cartridge.RunCycles(2 * RealTimeClock::CyclesPerSecond);
	Latch();

	EXPECT_EQ(0, ReadClock(RealTimeClock::Seconds));
	EXPECT_EQ(RealTimeClock::HaltBit, ReadClock(RealTimeClock::DaysHigh));
}

TEST(Mbc3EmulatorTest, EmulatorDrivesClockInEmulatedTime)
{
	auto cartridge = std::make_shared<Mbc3Cartridge>(BankNumberedRom(4), 0, true, true);
	Emulator emulator{ cartridge };
	auto& counters = cartridge->GetRealTimeClock()->GetState().Counters;

	// Just under and just over one second of frames
	const auto framesPerSecond = RealTimeClock::CyclesPerSecond / Graphics::FrameClocks;
	for (auto frame = 0; frame < framesPerSecond; frame++) emulator.GetFrame();
	EXPECT_EQ(0, counters[RealTimeClock::Seconds]);

	emulator.GetFrame();
	EXPECT_EQ(1, counters[RealTimeClock::Seconds]);
}
