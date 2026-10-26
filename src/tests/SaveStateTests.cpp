#include "stdafx.h"
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "../core/RealTimeClock.h"
#include "../core/SaveState.h"
#include "../core/Crc32.h"
#include "../core/StateStream.h"
#include "CartridgeTestHelpers.h"
#include "SaveFileTestHelpers.h"

// The save state format, and states that must be rejected without touching the emulator. SaveStateRoundTripTests.cpp
// checks that states restore everything

using namespace SaveFileTestHelpers;

namespace
{
	using Bytes = std::vector<unsigned char>;

	const char* const RomPath = "roms/cpu_instrs/01-special.gb";
	const size_t HeaderSize = 12;
	const size_t CrcSize = 4;

	std::shared_ptr<Cartridge> LoadRom(int ramBanks = 1)
	{
		return CartridgeFactory::LoadFromFile(RomPath, ramBanks);
	}

	void RunFrames(Emulator& emulator, int frames)
	{
		for (auto frame = 0; frame < frames; frame++) emulator.GetFrame();
	}

	uint32_t ReadU32(const Bytes& data, size_t offset)
	{
		return data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 | static_cast<uint32_t>(data[offset + 3]) << 24;
	}

	void WriteU32(Bytes& data, size_t offset, uint32_t value)
	{
		for (auto i = 0; i < 4; i++) data[offset + i] = static_cast<unsigned char>(value >> (8 * i));
	}

	// Makes the CRC match again after changing a state, so that the change itself is what's checked
	void FixCrc(Bytes& state)
	{
		WriteU32(state, state.size() - CrcSize, Crc32::Compute(state.data(), state.size() - CrcSize));
	}

	// The offset of a section's contents, found by walking the sections after the header
	size_t FindSection(const Bytes& state, const std::string& tag)
	{
		for (size_t offset = HeaderSize; offset + 8 <= state.size() - CrcSize; offset += 8 + ReadU32(state, offset + 4))
		{
			if (std::string(state.begin() + offset, state.begin() + offset + 4) == tag) return offset + 8;
		}

		ADD_FAILURE() << "No " << tag << " section";
		return 0;
	}

	class SaveStateTest : public testing::Test
	{
	protected:
		std::shared_ptr<Cartridge> Rom{ LoadRom() };
		std::unique_ptr<Emulator> Emu;
		Bytes State;

		void SetUp() override
		{
			ASSERT_NE(nullptr, Rom) << "ROM not found; run the tests from src/tests";
			Emu = std::make_unique<Emulator>(Rom);
			RunFrames(*Emu, 30);
			State = SaveState::Create(*Emu);
		}

		// Expects restoring the state to fail with a message containing the text, leaving the emulator as it was
		void ExpectRejected(const Bytes& state, const std::string& text)
		{
			auto target = std::make_unique<Emulator>(LoadRom());
			RunFrames(*target, 10);
			auto before = SaveState::Create(*target);

			auto error = SaveState::Restore(*target, state);

			EXPECT_FALSE(error.empty());
			EXPECT_NE(std::string::npos, error.find(text)) << "Error: \"" << error << "\"";
			EXPECT_TRUE(before == SaveState::Create(*target)) << "The emulator changed";
		}
	};
}

TEST(SaveStateCrcTest, MatchesTheStandardCheckValue)
{
	const std::string check = "123456789";
	EXPECT_EQ(0xcbf43926u, Crc32::Compute(reinterpret_cast<const unsigned char*>(check.data()), check.size()));
}

TEST_F(SaveStateTest, StartsWithMagicAndFormatVersion)
{
	EXPECT_EQ("EMUBOYSS", std::string(State.begin(), State.begin() + 8));
	EXPECT_EQ(SaveState::FormatVersion, ReadU32(State, 8));
	EXPECT_EQ(2u, SaveState::FormatVersion);
}

TEST_F(SaveStateTest, EndsWithCrcOfEverythingElse)
{
	EXPECT_EQ(Crc32::Compute(State.data(), State.size() - CrcSize), ReadU32(State, State.size() - CrcSize));
}

TEST_F(SaveStateTest, IdentifiesTheRomByTitleSizeAndCrc)
{
	auto rom = FindSection(State, "ROM ");
	auto& romData = Rom->GetRom();

	EXPECT_EQ(Bytes(romData.begin() + 0x134, romData.begin() + 0x144), Bytes(State.begin() + rom, State.begin() + rom + 16));
	EXPECT_EQ(romData.size(), ReadU32(State, rom + 16));
	EXPECT_EQ(Crc32::Compute(romData.data(), romData.size()), ReadU32(State, rom + 20));
	EXPECT_EQ(24u, ReadU32(State, rom - 4));
}

TEST_F(SaveStateTest, HoldsASectionForEachComponentInOrder)
{
	std::vector<std::string> tags;
	for (size_t offset = HeaderSize; offset < State.size() - CrcSize; offset += 8 + ReadU32(State, offset + 4))
	{
		tags.emplace_back(State.begin() + offset, State.begin() + offset + 4);
	}

	EXPECT_EQ((std::vector<std::string>{ "ROM ", "CPU ", "MEM ", "PPU ", "DMA ", "TIMR", "APU ", "SERL", "JOYP", "CART" }), tags);
}

TEST_F(SaveStateTest, SavingTwiceGivesTheSameState)
{
	EXPECT_TRUE(State == SaveState::Create(*Emu));
}

TEST_F(SaveStateTest, RestoresIntoANewEmulator)
{
	Emulator restored{ LoadRom() };

	ASSERT_EQ("", SaveState::Restore(restored, State));

	EXPECT_TRUE(Emu->GetCpu().GetRegisters() == restored.GetCpu().GetRegisters());
	EXPECT_EQ(Emu->GetCpuClocks(), restored.GetCpuClocks());
	EXPECT_TRUE(State == SaveState::Create(restored));
}

TEST_F(SaveStateTest, SavesAndLoadsWithoutAnyLongDelay)
{
	// Not a benchmark: each takes well under a millisecond on a desktop, so this only catches something badly wrong,
	// however slow or busy the machine running the tests
	const auto limit = std::chrono::seconds(1);
	Emulator restored{ LoadRom() };

	auto start = std::chrono::steady_clock::now();
	auto state = SaveState::Create(*Emu);
	auto saved = std::chrono::steady_clock::now();
	auto error = SaveState::Restore(restored, state);
	auto loaded = std::chrono::steady_clock::now();

	EXPECT_EQ("", error);
	EXPECT_LT(saved - start, limit);
	EXPECT_LT(loaded - saved, limit);
}

TEST_F(SaveStateTest, RejectsEmptyData)
{
	ExpectRejected({}, "isn't an Emu Boy save state");
}

TEST_F(SaveStateTest, RejectsDataWithoutTheMagic)
{
	auto state = State;
	state[0] = 'X';

	ExpectRejected(state, "isn't an Emu Boy save state");
}

TEST_F(SaveStateTest, RejectsAnotherFormatVersion)
{
	auto state = State;
	WriteU32(state, 8, SaveState::FormatVersion + 1);

	ExpectRejected(state, "version " + std::to_string(SaveState::FormatVersion + 1) + " of the format");
}

TEST_F(SaveStateTest, RejectsAnotherFormatVersionEvenIfItsLaidOutDifferently)
{
	Bytes state{ 'E', 'M', 'U', 'B', 'O', 'Y', 'S', 'S', 99, 0, 0, 0, 1, 2, 3 };

	ExpectRejected(state, "version 99 of the format");
}

TEST_F(SaveStateTest, RejectsTruncatedStates)
{
	for (auto size : { size_t{ 0 }, size_t{ 7 }, size_t{ 8 }, size_t{ 11 }, size_t{ 12 }, size_t{ 15 }, size_t{ 16 }, size_t{ 40 },
		State.size() / 2, State.size() - CrcSize, State.size() - 1 })
	{
		SCOPED_TRACE(size);
		ExpectRejected(Bytes(State.begin(), State.begin() + size),
			size < 8 ? "isn't an Emu Boy save state" : size < 16 ? "truncated" : "checksum doesn't match");
	}
}

TEST_F(SaveStateTest, RejectsTruncatedStatesEvenWithAMatchingCrc)
{
	for (auto size : { size_t{ 20 }, size_t{ 100 }, State.size() / 3, State.size() - CrcSize - 1 })
	{
		SCOPED_TRACE(size);
		Bytes state(State.begin(), State.begin() + size);
		state.resize(size + CrcSize);
		FixCrc(state);

		ExpectRejected(state, "The save state is invalid");
	}
}

TEST_F(SaveStateTest, RejectsCorruptStates)
{
	for (auto offset : { HeaderSize, State.size() / 2, State.size() - CrcSize - 1, State.size() - 1 })
	{
		SCOPED_TRACE(offset);
		auto state = State;
		state[offset] ^= 0x10;

		ExpectRejected(state, "checksum doesn't match");
	}
}

TEST_F(SaveStateTest, RejectsAStateForAnotherGame)
{
	Emulator other{ CartridgeFactory::LoadFromFile("roms/dmg-acid2/dmg-acid2.gb", 1) };

	ExpectRejected(SaveState::Create(other), "for another game (DMG-ACID2)");
}

TEST_F(SaveStateTest, RejectsAStateForAnotherUntitledGame)
{
	// blargg's ROMs have no title, so are told apart by their contents
	Emulator other{ CartridgeFactory::LoadFromFile("roms/cpu_instrs/02-interrupts.gb", 1) };

	ExpectRejected(SaveState::Create(other), "for another game (untitled)");
}

TEST_F(SaveStateTest, RejectsAStateForAnotherVersionOfTheGame)
{
	// The same header, but different code: a byte past the header changed
	auto rom = Rom->GetRom();
	rom[0x200] ^= 1;
	Emulator other{ CartridgeFactory::Create(std::move(rom), 1) };

	ExpectRejected(SaveState::Create(other), "for another game");
}

TEST_F(SaveStateTest, RejectsAStateWithAnotherAmountOfCartridgeRam)
{
	Emulator other{ LoadRom(4) };

	ExpectRejected(SaveState::Create(other), "32768 bytes of cartridge RAM rather than 8192");
}

TEST_F(SaveStateTest, RejectsOutOfRangeValues)
{
	// The CPU's state (running, halted or stopped) follows its six register pairs
	auto state = State;
	WriteU32(state, FindSection(state, "CPU ") + 12, 3);
	FixCrc(state);

	ExpectRejected(state, "invalid in its CPU section: a value is 3, outside 0 to 2");
}

TEST_F(SaveStateTest, RejectsAPpuEventThatCantHappenOnItsLine)
{
	// The line and the next event in it follow VRAM, OAM and the 12 registers. Drawing on a V-Blank line would draw
	// past the end of the frame
	const size_t lineOffset = 0x2000 + 160 + 12;
	const size_t eventOffset = lineOffset + 16;
	const uint32_t drawEvent = 2;

	auto state = State;
	auto ppu = FindSection(state, "PPU ");
	WriteU32(state, ppu + lineOffset, 150);
	WriteU32(state, ppu + eventOffset, drawEvent);
	FixCrc(state);

	ExpectRejected(state, "invalid in its PPU section: its next event can't happen on line 150");
}

TEST_F(SaveStateTest, RejectsASectionWithAnotherLength)
{
	auto state = State;
	auto cpu = FindSection(state, "CPU ");
	WriteU32(state, cpu - 4, ReadU32(state, cpu - 4) + 1);
	FixCrc(state);

	ExpectRejected(state, "The save state is invalid");
}

TEST_F(SaveStateTest, RejectsMissingSections)
{
	auto state = State;
	auto dma = FindSection(state, "DMA ") - 8;
	auto timer = FindSection(state, "TIMR") - 8;
	state.erase(state.begin() + dma, state.begin() + timer);
	FixCrc(state);

	ExpectRejected(state, "expected the DMA section");
}

TEST_F(SaveStateTest, RejectsExtraDataAfterTheSections)
{
	auto state = State;
	state.insert(state.end() - CrcSize, 0);
	FixCrc(state);

	ExpectRejected(state, "it holds more than was read");
}

TEST_F(SaveStateTest, FailingLateInTheStateLeavesTheEmulatorRunningAsBefore)
{
	// The cartridge comes last, so every other component has been read before its RAM size is found to differ
	auto target = std::make_unique<Emulator>(LoadRom(4));
	auto twin = std::make_unique<Emulator>(LoadRom(4));
	RunFrames(*target, 380);
	RunFrames(*twin, 380);

	EXPECT_NE("", SaveState::Restore(*target, State));

	for (auto frame = 0; frame < 20; frame++)
	{
		auto expected = twin->GetFrame();
		auto actual = target->GetFrame();
		ASSERT_TRUE(std::equal(expected, expected + Graphics::HozPixels * Graphics::VertPixels, actual)) << "Frame " << frame;
	}

	EXPECT_TRUE(SaveState::Create(*twin) == SaveState::Create(*target));
}

TEST(SaveStateCartridgeTest, RestoresBatteryRamAndMarksItForSaving)
{
	auto rom = Rom(Mbc1RamBattery, Ram32K);
	auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
	Emulator emulator{ cartridge };
	FillRam(*cartridge, 3);
	auto savedRam = cartridge->GetRam();
	auto state = SaveState::Create(emulator);

	FillRam(*cartridge, 5);
	cartridge->MarkSaved();
	ASSERT_EQ("", SaveState::Restore(emulator, state));

	EXPECT_EQ(savedRam, cartridge->GetRam());
	EXPECT_TRUE(cartridge->HasUnsavedChanges());
}

TEST(SaveStateCartridgeTest, RestoresTheClockAsItWas)
{
	auto rom = Rom(Mbc3TimerRamBattery, Ram8K);
	auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
	Emulator emulator{ cartridge };
	auto& clock = *cartridge->GetRealTimeClock();
	clock.WriteRegister(RealTimeClock::Hours, 5);
	clock.WriteRegister(RealTimeClock::Minutes, 6);
	clock.RunCycles(RealTimeClock::CyclesPerSecond / 3);
	clock.WriteLatch(0);
	auto saved = clock.GetState();
	auto state = SaveState::Create(emulator);

	clock.AdvanceSeconds(100000);
	clock.WriteLatch(1);
	ASSERT_EQ("", SaveState::Restore(emulator, state));

	auto& restored = clock.GetState();
	EXPECT_EQ(saved.Counters, restored.Counters);
	EXPECT_EQ(saved.Latched, restored.Latched);
	EXPECT_EQ(saved.CyclesIntoSecond, restored.CyclesIntoSecond);
	EXPECT_TRUE(restored.LatchArmed);
}

namespace
{
	// Writes to a mapper's registers, saves, and restores into a new cartridge of the same ROM, which it returns
	std::shared_ptr<Cartridge> SaveAndRestoreMapper(unsigned char cartridgeType, unsigned char ramSize, int romBanks,
		const std::vector<std::pair<unsigned short, unsigned char>>& writes)
	{
		auto rom = CartridgeTestHelpers::BankNumberedRom(romBanks);
		rom[CartridgeFactory::CartridgeTypeHeaderOffset] = cartridgeType;
		rom[CartridgeFactory::RamSizeHeaderOffset] = ramSize;

		auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
		Emulator emulator{ cartridge };
		for (auto& write : writes) cartridge->RomWriteByte(write.first, write.second);
		cartridge->RamWriteByte(0x1234, 0x5a);
		auto state = SaveState::Create(emulator);

		auto restored = CartridgeFactory::Create(std::vector<unsigned char>(rom));
		Emulator restoredEmulator{ restored };
		EXPECT_EQ("", SaveState::Restore(restoredEmulator, state));

		// The RAM bank selected, and whether RAM is enabled
		EXPECT_EQ(cartridge->RamReadByte(0x1234), restored->RamReadByte(0x1234));
		EXPECT_EQ(cartridge->RamReadByte(0x0234), restored->RamReadByte(0x0234));
		return restored;
	}
}

TEST(SaveStateCartridgeTest, RestoresMbc1Registers)
{
	// RAM enabled, BANK1 5, BANK2 1 and mode 1, which maps bank $20 at $0000 and $25 at $4000
	auto cartridge = SaveAndRestoreMapper(Mbc1RamBattery, Ram32K, 64, { { 0x0000, 0x0a }, { 0x2000, 5 }, { 0x4000, 1 }, { 0x6000, 1 } });
	EXPECT_EQ(0x20, CartridgeTestHelpers::Bank0(*cartridge));
	EXPECT_EQ(0x25, CartridgeTestHelpers::Bank1(*cartridge));

	// BANK1 still holds 5 when BANK2 changes
	cartridge->RomWriteByte(0x4000, 0);
	EXPECT_EQ(0x05, CartridgeTestHelpers::Bank1(*cartridge));
}

TEST(SaveStateCartridgeTest, RestoresMbc2Registers)
{
	auto cartridge = SaveAndRestoreMapper(Mbc2Battery, NoRam, 16, { { 0x0000, 0x0a }, { 0x0100, 9 } });
	EXPECT_EQ(9, CartridgeTestHelpers::Bank1(*cartridge));
}

TEST(SaveStateCartridgeTest, RestoresMbc3Registers)
{
	auto cartridge = SaveAndRestoreMapper(Mbc3TimerRamBattery, Ram32K, 128, { { 0x0000, 0x0a }, { 0x2000, 0x55 }, { 0x4000, 2 } });
	EXPECT_EQ(0x55, CartridgeTestHelpers::Bank1(*cartridge));
}

TEST(SaveStateCartridgeTest, RestoresMbc5Registers)
{
	auto cartridge = SaveAndRestoreMapper(Mbc5RamBattery, Ram128K, 512, { { 0x0000, 0x0a }, { 0x2000, 0x34 }, { 0x3000, 1 }, { 0x4000, 7 } });
	EXPECT_EQ(0x134, CartridgeTestHelpers::Bank1(*cartridge));

	// The ninth bit is kept when the lower eight change
	cartridge->RomWriteByte(0x2000, 0x12);
	EXPECT_EQ(0x112, CartridgeTestHelpers::Bank1(*cartridge));
}

TEST(SaveStateCartridgeTest, CloneSharesTheRomButNotTheRegistersOrRam)
{
	auto rom = CartridgeTestHelpers::BankNumberedRom(8);
	rom[CartridgeFactory::CartridgeTypeHeaderOffset] = Mbc1RamBattery;
	rom[CartridgeFactory::RamSizeHeaderOffset] = Ram8K;
	auto cartridge = CartridgeFactory::Create(std::move(rom));
	cartridge->RomWriteByte(0x0000, 0x0a);
	cartridge->RomWriteByte(0x2000, 3);
	cartridge->RamWriteByte(0, 0x12);

	auto clone = cartridge->Clone();
	clone->RomWriteByte(0x2000, 5);
	clone->RamWriteByte(0, 0x34);

	EXPECT_EQ(&cartridge->GetRom(), &clone->GetRom());
	EXPECT_EQ(3, CartridgeTestHelpers::Bank1(*cartridge));
	EXPECT_EQ(5, CartridgeTestHelpers::Bank1(*clone));
	EXPECT_EQ(0x12, cartridge->RamReadByte(0));
	EXPECT_EQ(0x34, clone->RamReadByte(0));
}
