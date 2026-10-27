#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/SaveFile.h"
#include "../core/CartridgeFactory.h"
#include "../core/RealTimeClock.h"
#include "SaveFileTestHelpers.h"

using namespace SaveFileTestHelpers;

// Expectations from the RTC footer that BGB, VBA-M, mGBA (GBMBCRTCSaveBuffer) and SameBoy (rtc_save_t) share: five
// 32-bit little-endian clock registers, five latched registers, then a 64-bit (or older 32-bit) UNIX time stamp

namespace
{
	const int64_t SaveTime = 1700000000;

	using Registers = std::array<unsigned char, RealTimeClock::RegisterCount>;

	void AppendLittleEndian(std::vector<unsigned char>& out, uint64_t value, int bytes)
	{
		for (auto i = 0; i < bytes; i++) out.push_back(static_cast<unsigned char>(value >> (8 * i)));
	}

	// A save for an MBC3 clock cartridge without RAM, built byte by byte from the documented layout
	std::vector<unsigned char> ClockSave(const Registers& counters, const Registers& latched, uint64_t time, int timeBytes = 8)
	{
		std::vector<unsigned char> save;
		for (auto value : counters) AppendLittleEndian(save, value, 4);
		for (auto value : latched) AppendLittleEndian(save, value, 4);
		AppendLittleEndian(save, time, timeBytes);
		return save;
	}

	Registers Counters(Cartridge& cartridge)
	{
		return cartridge.GetRealTimeClock()->GetState().Counters;
	}

	void SetCounters(Cartridge& cartridge, const Registers& counters, const Registers& latched)
	{
		RealTimeClock::State state;
		state.Counters = counters;
		state.Latched = latched;
		cartridge.GetRealTimeClock()->SetState(state);
	}

	// Loads a clock save made at SaveTime, secondsLater
	std::shared_ptr<Cartridge> LoadClockSave(const Registers& counters, int64_t secondsLater)
	{
		auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));
		SaveFile::Deserialise(*cartridge, ClockSave(counters, {}, SaveTime), SaveTime + secondsLater);
		return cartridge;
	}
}

TEST(SaveFileTest, RamIsSavedAsRawDump)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	FillRam(*cartridge, 3);

	auto save = SaveFile::Serialise(*cartridge, SaveTime);

	EXPECT_EQ(cartridge->GetRam(), save);
}

TEST(SaveFileTest, RamRoundTrips)
{
	auto original = CartridgeFactory::Create(Rom(Mbc5RamBattery, Ram128K));
	FillRam(*original, 7);
	auto restored = CartridgeFactory::Create(Rom(Mbc5RamBattery, Ram128K));

	auto warning = SaveFile::Deserialise(*restored, SaveFile::Serialise(*original, SaveTime), SaveTime);

	EXPECT_EQ(original->GetRam(), restored->GetRam());
	EXPECT_EQ("", warning);
}

TEST(SaveFileTest, LoadingClearsUnsavedChanges)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0x0000, 0x12);

	SaveFile::Deserialise(*cartridge, std::vector<unsigned char>(Ram8KSize), SaveTime);

	EXPECT_FALSE(cartridge->HasUnsavedChanges());
}

TEST(SaveFileTest, Mbc2SavesOneBytePerNibbleWithUpperBitsSet)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc2Battery, NoRam));
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0x0000, 0x05);
	cartridge->RamWriteByte(0x01ff, 0x0a);

	auto save = SaveFile::Serialise(*cartridge, SaveTime);

	ASSERT_EQ(512u, save.size());
	EXPECT_EQ(0xf5, save[0x000]);
	EXPECT_EQ(0xfa, save[0x1ff]);
	EXPECT_EQ(0xf0, save[0x100]);
}

TEST(SaveFileTest, Mbc2LoadIgnoresUpperNibbles)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc2Battery, NoRam));
	std::vector<unsigned char> save(512, 0x00);
	save[0x000] = 0x0c;
	save[0x1ff] = 0x3b;

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EnableRam(*cartridge);
	EXPECT_EQ(0xfc, cartridge->RamReadByte(0x0000));
	EXPECT_EQ(0xfb, cartridge->RamReadByte(0x01ff));
	EXPECT_EQ(0x0b, cartridge->GetRam()[0x1ff]);
	EXPECT_EQ("", warning);
}

TEST(SaveFileTest, Mbc2LoadsMgbaPackedNibbles)
{
	// mGBA keeps MBC2 RAM as 256 bytes: byte i holds address 2i in its lower nibble and 2i+1 in its upper one
	auto cartridge = CartridgeFactory::Create(Rom(Mbc2Battery, NoRam));
	std::vector<unsigned char> save(256, 0x00);
	save[0x00] = 0x21;
	save[0x80] = 0x9e;
	save[0xff] = 0x5c;

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EnableRam(*cartridge);
	EXPECT_EQ(0xf1, cartridge->RamReadByte(0x0000));
	EXPECT_EQ(0xf2, cartridge->RamReadByte(0x0001));
	EXPECT_EQ(0xfe, cartridge->RamReadByte(0x0100));
	EXPECT_EQ(0xf9, cartridge->RamReadByte(0x0101));
	EXPECT_EQ(0xfc, cartridge->RamReadByte(0x01fe));
	EXPECT_EQ(0xf5, cartridge->RamReadByte(0x01ff));
	EXPECT_EQ(0xf0, cartridge->RamReadByte(0x0002));
	EXPECT_EQ("", warning);
}

TEST(SaveFileTest, Mbc2RoundTrips)
{
	auto original = CartridgeFactory::Create(Rom(Mbc2Battery, NoRam));
	EnableRam(*original);
	for (unsigned short address = 0; address < 512; address++) original->RamWriteByte(address, address % 16);
	auto restored = CartridgeFactory::Create(Rom(Mbc2Battery, NoRam));

	SaveFile::Deserialise(*restored, SaveFile::Serialise(*original, SaveTime), SaveTime);

	EXPECT_EQ(original->GetRam(), restored->GetRam());
}

TEST(SaveFileTest, ClockFooterFollowsRam)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram32K));
	FillRam(*cartridge, 5);
	SetCounters(*cartridge, { { 1, 2, 3, 4, 0x41 } }, { { 5, 6, 7, 8, 0x01 } });

	auto save = SaveFile::Serialise(*cartridge, 0x0102030405060708);

	auto expected = cartridge->GetRam();
	// The clock registers go in the latched fields too, as mGBA reads only those
	auto footer = ClockSave({ { 1, 2, 3, 4, 0x41 } }, { { 1, 2, 3, 4, 0x41 } }, 0x0102030405060708);
	expected.insert(expected.end(), footer.begin(), footer.end());
	EXPECT_EQ(expected, save);
	EXPECT_EQ(32768u + 48, save.size());
}

TEST(SaveFileTest, MgbaReadsTheCurrentTimeFromTheLatchedFields)
{
	// mGBA restores its clock from the latched fields and the time stamp alone, so they must hold the current time
	// even when the game last latched long ago
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));
	SetCounters(*cartridge, { { 30, 45, 12, 100, 0 } }, { { 0, 0, 0, 0, 0 } });

	auto save = SaveFile::Serialise(*cartridge, SaveTime);

	EXPECT_EQ(ClockSave({ { 30, 45, 12, 100, 0 } }, { { 30, 45, 12, 100, 0 } }, SaveTime), save);
}

TEST(SaveFileTest, MgbaSaveLoadsItsClockRegisters)
{
	// mGBA's latched fields and time stamp are from the game's last latch, and its clock register fields from when it
	// saved. Emu Boy pairs the clock registers with the time stamp, so runs ahead by the time between the two
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));
	const auto latchTime = SaveTime - 20;

	SaveFile::Deserialise(*cartridge, ClockSave({ { 20, 0, 0, 0, 0 } }, { { 0, 0, 0, 0, 0 } }, latchTime), SaveTime);

	EXPECT_EQ((Registers{ { 40, 0, 0, 0, 0 } }), Counters(*cartridge));
}

TEST(SaveFileTest, ClockOnlyCartridgeSavesJustTheFooter)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));

	EXPECT_EQ(48u, SaveFile::Serialise(*cartridge, SaveTime).size());
}

TEST(SaveFileTest, ClockAndRamRoundTrip)
{
	auto original = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram32K));
	FillRam(*original, 9);
	SetCounters(*original, { { 59, 59, 23, 0xff, 0x81 } }, { { 10, 20, 5, 0x34, 0x00 } });
	auto restored = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram32K));

	auto warning = SaveFile::Deserialise(*restored, SaveFile::Serialise(*original, SaveTime), SaveTime);

	EXPECT_EQ(original->GetRam(), restored->GetRam());
	EXPECT_EQ(original->GetRealTimeClock()->GetState().Counters, restored->GetRealTimeClock()->GetState().Counters);
	EXPECT_EQ(original->GetRealTimeClock()->GetState().Counters, restored->GetRealTimeClock()->GetState().Latched);
	EXPECT_EQ("", warning);
}

TEST(SaveFileTest, LoadsLegacyFooterWith32BitTime)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));

	auto warning = SaveFile::Deserialise(*cartridge, ClockSave({ { 10, 0, 0, 0, 0 } }, { { 1, 2, 3, 4, 0 } }, SaveTime, 4),
		SaveTime + 90);

	EXPECT_EQ((Registers{ { 40, 1, 0, 0, 0 } }), Counters(*cartridge));
	EXPECT_EQ((Registers{ { 1, 2, 3, 4, 0 } }), cartridge->GetRealTimeClock()->GetState().Latched);
	EXPECT_EQ("", warning);
}

TEST(SaveFileTest, ClockCatchesUpWithElapsedTime)
{
	auto cartridge = LoadClockSave({ { 59, 59, 23, 0, 0 } }, 1);

	EXPECT_EQ((Registers{ { 0, 0, 0, 1, 0 } }), Counters(*cartridge));
}

TEST(SaveFileTest, ClockCatchesUpWithDaysAway)
{
	auto cartridge = LoadClockSave({ { 30, 15, 6, 0xfe, 0 } }, 3 * 24 * 60 * 60 + 60);

	// Day 254 + 3 is 257, which needs the day counter's ninth bit
	EXPECT_EQ((Registers{ { 30, 16, 6, 0x01, RealTimeClock::DayHighBit } }), Counters(*cartridge));
}

TEST(SaveFileTest, CatchingUpLeavesLatchedRegisters)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));

	SaveFile::Deserialise(*cartridge, ClockSave({ { 0, 0, 0, 0, 0 } }, { { 7, 8, 9, 10, 0 } }, SaveTime), SaveTime + 1000);

	EXPECT_EQ((Registers{ { 7, 8, 9, 10, 0 } }), cartridge->GetRealTimeClock()->GetState().Latched);
}

TEST(SaveFileTest, HaltedClockDoesNotCatchUp)
{
	auto cartridge = LoadClockSave({ { 5, 4, 3, 2, RealTimeClock::HaltBit } }, 100000);

	EXPECT_EQ((Registers{ { 5, 4, 3, 2, RealTimeClock::HaltBit } }), Counters(*cartridge));
}

TEST(SaveFileTest, ClockSetBackDoesNotWindTheClockBack)
{
	auto cartridge = LoadClockSave({ { 5, 4, 3, 2, 0 } }, -3600);

	EXPECT_EQ((Registers{ { 5, 4, 3, 2, 0 } }), Counters(*cartridge));
}

TEST(SaveFileTest, GapBeyondDayCounterSetsCarry)
{
	auto cartridge = LoadClockSave({ { 0, 0, 0, 0, 0 } }, 600LL * 24 * 60 * 60);

	// 600 days wraps the 512-day counter to day 88 and sets the carry bit, as on the cartridge
	EXPECT_EQ((Registers{ { 0, 0, 0, 88, RealTimeClock::DayCarryBit } }), Counters(*cartridge));
}

TEST(SaveFileTest, ZeroTimeStampDoesNotMoveTheClock)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));

	SaveFile::Deserialise(*cartridge, ClockSave({ { 5, 4, 3, 2, 0 } }, {}, 0), SaveTime);

	EXPECT_EQ((Registers{ { 5, 4, 3, 2, 0 } }), Counters(*cartridge));
}

TEST(SaveFileTest, TimeStampBefore1997DoesNotMoveTheClock)
{
	// There were no clock cartridges before 1997, so an earlier time stamp is garbage
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));

	SaveFile::Deserialise(*cartridge, ClockSave({ { 5, 4, 3, 2, 0 } }, {}, 852076799), SaveTime);

	EXPECT_EQ((Registers{ { 5, 4, 3, 2, 0 } }), Counters(*cartridge));
}

TEST(SaveFileTest, TimeStampFrom1997MovesTheClock)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));

	SaveFile::Deserialise(*cartridge, ClockSave({ { 5, 4, 3, 2, 0 } }, {}, 852076800), 852076800 + 10);

	EXPECT_EQ((Registers{ { 15, 4, 3, 2, 0 } }), Counters(*cartridge));
}

TEST(SaveFileTest, ClockRegistersAreMaskedToTheirBits)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));
	auto save = ClockSave({}, {}, SaveTime);
	std::fill(save.begin(), save.begin() + 40, 0xff);

	SaveFile::Deserialise(*cartridge, save, SaveTime);

	// Per https://gbdev.io/pandocs/MBC3.html, seconds and minutes have 6 bits, hours 5 and the day high register
	// bits 0, 6 and 7
	Registers expected{ { 0x3f, 0x3f, 0x1f, 0xff, 0xc1 } };
	EXPECT_EQ(expected, Counters(*cartridge));
	EXPECT_EQ(expected, cartridge->GetRealTimeClock()->GetState().Latched);
}

TEST(SaveFileTest, ShortSaveLoadsWhatItHasAndWarns)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram32K));
	std::vector<unsigned char> save(Ram8KSize, 0x5a);

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_EQ(0x5a, cartridge->GetRam()[Ram8KSize - 1]);
	EXPECT_EQ(0x00, cartridge->GetRam()[Ram8KSize]);
	EXPECT_NE("", warning);
}

TEST(SaveFileTest, LongSaveLoadsWhatFitsAndWarns)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	std::vector<unsigned char> save(Ram8KSize + 100, 0x5a);
	save[Ram8KSize - 1] = 0x77;

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_EQ(Ram8KSize, cartridge->GetRam().size());
	EXPECT_EQ(0x77, cartridge->GetRam()[Ram8KSize - 1]);
	EXPECT_NE("", warning);
}

TEST(SaveFileTest, EmptySaveWarns)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	EXPECT_NE("", SaveFile::Deserialise(*cartridge, {}, SaveTime));
}

TEST(SaveFileTest, ClockCartridgeSaveWithoutFooterLoadsRamAndWarns)
{
	// As written by emulators that don't save the clock
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram32K));
	std::vector<unsigned char> save(4 * Ram8KSize, 0x42);

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_EQ(save, cartridge->GetRam());
	EXPECT_EQ((Registers{}), Counters(*cartridge));
	EXPECT_NE("", warning);
}

TEST(SaveFileTest, TwoKbRamIsSavedAs2048Bytes)
{
	// As BGB, SameBoy and VBA-M save RAM size code 1
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	FillRam(*cartridge, 7);

	auto save = SaveFile::Serialise(*cartridge, SaveTime);

	EXPECT_EQ(Ram2KSize, save.size());
	EXPECT_EQ(cartridge->GetRam(), save);
}

TEST(SaveFileTest, TwoKbRamRoundTrips)
{
	auto original = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	FillRam(*original, 7);
	auto restored = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));

	EXPECT_EQ("", SaveFile::Deserialise(*restored, SaveFile::Serialise(*original, SaveTime), SaveTime));
	EXPECT_EQ(original->GetRam(), restored->GetRam());
}

TEST(SaveFileTest, TwoKbRamLoadsMgbaEightKbSaveWithoutWarning)
{
	// mGBA saves RAM size code 1 as a whole 8 KB bank, whose first 2 KB the game uses
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	std::vector<unsigned char> save(Ram8KSize, 0xff);
	for (size_t i = 0; i < Ram2KSize; i++) save[i] = static_cast<unsigned char>(i * 3);

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_EQ("", warning);
	EXPECT_EQ(std::vector<unsigned char>(save.begin(), save.begin() + Ram2KSize), cartridge->GetRam());
}

TEST(SaveFileTest, TwoKbRamLoadsMgbaEightKbSaveMirroringItsFirst2KbWithoutWarning)
{
	// Writes through the mirrors at $A800-$BFFF, as mGBA has no mirroring, leave copies of the first 2 KB
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	std::vector<unsigned char> save(Ram8KSize);
	for (size_t i = 0; i < Ram8KSize; i++) save[i] = static_cast<unsigned char>(i % Ram2KSize * 5);

	EXPECT_EQ("", SaveFile::Deserialise(*cartridge, save, SaveTime));
	EXPECT_EQ(std::vector<unsigned char>(save.begin(), save.begin() + Ram2KSize), cartridge->GetRam());
}

TEST(SaveFileTest, TwoKbRamWarnsWhenMgbaEightKbSaveHoldsMoreData)
{
	// A homebrew ROM that declares 2 KB but uses more has real data above 2 KB in mGBA, which can't be loaded
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	std::vector<unsigned char> save(Ram8KSize, 0x11);
	std::fill(save.begin() + Ram2KSize, save.end(), 0x77);

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_NE("", warning);
	EXPECT_EQ(std::vector<unsigned char>(Ram2KSize, 0x11), cartridge->GetRam());
}

TEST(SaveFileTest, TwoKbClockCartridgeLoadsMgbaEightKbSaveWithFooter)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram2K));
	const Registers counters{ { 5, 6, 7, 8, 0 } };
	std::vector<unsigned char> save(Ram8KSize, 0x42);
	auto footer = ClockSave(counters, counters, SaveTime);
	save.insert(save.end(), footer.begin(), footer.end());

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_EQ("", warning);
	EXPECT_EQ(std::vector<unsigned char>(Ram2KSize, 0x42), cartridge->GetRam());
	EXPECT_EQ(counters, Counters(*cartridge));
}

TEST(SaveFileTest, TwoKbRamWarnsAboutOtherSizes)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	std::vector<unsigned char> save(2 * Ram2KSize, 0x5a);

	auto warning = SaveFile::Deserialise(*cartridge, save, SaveTime);

	EXPECT_EQ(std::vector<unsigned char>(Ram2KSize, 0x5a), cartridge->GetRam());
	EXPECT_NE("", warning);
}

TEST(SaveFileTest, EightKbRamWarnsAboutTwoKbSave)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	EXPECT_NE("", SaveFile::Deserialise(*cartridge, std::vector<unsigned char>(Ram2KSize, 0x5a), SaveTime));
}

TEST(SaveFileTest, ExpectedSizeIncludesClockFooter)
{
	EXPECT_EQ(Ram2KSize, SaveFile::ExpectedSize(*CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K))));
	EXPECT_EQ(Ram8KSize, SaveFile::ExpectedSize(*CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K))));
	EXPECT_EQ(512u, SaveFile::ExpectedSize(*CartridgeFactory::Create(Rom(Mbc2Battery, NoRam))));
	EXPECT_EQ(4 * Ram8KSize + 48, SaveFile::ExpectedSize(*CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram32K))));
	EXPECT_EQ(48u, SaveFile::ExpectedSize(*CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam))));
}
