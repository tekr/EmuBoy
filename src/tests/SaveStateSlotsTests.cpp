#include "stdafx.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "../core/CartridgeFactory.h"
#include "../core/SaveState.h"
#include "../frontend/SaveStateSlots.h"
#include "TestFiles.h"

using namespace TestFiles;

namespace
{
	const char* const RomPath = "roms/cpu_instrs/01-special.gb";

	std::unique_ptr<Emulator> PowerOn(const std::string& romPath = RomPath)
	{
		auto cartridge = CartridgeFactory::LoadFromFileWithRamSize(romPath, Cartridge::RamBankSize);
		return cartridge == nullptr ? nullptr : std::make_unique<Emulator>(cartridge, GameBoyModel::Dmg);
	}

	void RunFrames(Emulator& emulator, int frames)
	{
		for (auto frame = 0; frame < frames; frame++) emulator.GetFrame();
	}

	// Slots for a ROM in a fresh directory. The ROM itself needn't be there, only its slots' files
	class SaveStateSlotsTest : public testing::Test
	{
	protected:
		TestDirectory Directory{ "SaveStateSlotsTest" };
		SaveStateSlots Slots{ Directory.File("game.gb") };
		std::unique_ptr<Emulator> GameBoy{ PowerOn() };

		void SetUp() override
		{
			ASSERT_NE(nullptr, GameBoy) << "ROM not found; run the tests from src/tests";
			for (auto slot = 1; slot <= SaveStateSlots::SlotCount; slot++) Directory.File("game.ss" + std::to_string(slot));
			RunFrames(*GameBoy, 20);
		}

		// Expects loading to fail with the message, leaving the emulator as it was
		void ExpectLoadFails(const std::string& message)
		{
			auto before = SaveState::Create(*GameBoy);

			auto result = Slots.Load(*GameBoy);

			EXPECT_FALSE(result.Succeeded);
			EXPECT_EQ(0u, result.Message.find(message)) << result.Message;
			EXPECT_TRUE(before == SaveState::Create(*GameBoy)) << "The emulator changed";
		}
	};
}

TEST(SaveStateSlotsPathTest, NamesSlotsAfterTheRom)
{
	SaveStateSlots slots{ "roms/Tetris.gb" };

	EXPECT_EQ("roms/Tetris.ss1", slots.PathForSlot(1));
	EXPECT_EQ("roms/Tetris.ss4", slots.PathForSlot(4));
}

TEST(SaveStateSlotsPathTest, StartsAtSlotOneAndWrapsAround)
{
	SaveStateSlots slots{ "Tetris.gb" };
	EXPECT_EQ(1, slots.GetSlot());

	slots.PreviousSlot();
	EXPECT_EQ(SaveStateSlots::SlotCount, slots.GetSlot());

	slots.NextSlot();
	EXPECT_EQ(1, slots.GetSlot());

	for (auto slot = 2; slot <= SaveStateSlots::SlotCount; slot++)
	{
		slots.NextSlot();
		EXPECT_EQ(slot, slots.GetSlot());
	}

	slots.NextSlot();
	EXPECT_EQ(1, slots.GetSlot());
}

TEST_F(SaveStateSlotsTest, SavesTheStateInTheSelectedSlotsFile)
{
	Slots.NextSlot();

	auto result = Slots.Save(*GameBoy);

	EXPECT_TRUE(result.Succeeded);
	EXPECT_EQ("Saved state 2", result.Message);
	EXPECT_TRUE(SaveState::Create(*GameBoy) == ReadAll(Slots.PathForSlot(2)));
	EXPECT_FALSE(Exists(Slots.PathForSlot(1)));
	EXPECT_FALSE(Exists(Slots.PathForSlot(2) + ".tmp"));
}

TEST_F(SaveStateSlotsTest, LoadsTheStateFromTheSelectedSlot)
{
	auto saved = SaveState::Create(*GameBoy);
	ASSERT_TRUE(Slots.Save(*GameBoy).Succeeded);
	RunFrames(*GameBoy, 10);

	auto result = Slots.Load(*GameBoy);

	EXPECT_TRUE(result.Succeeded);
	EXPECT_EQ("Loaded state 1", result.Message);
	EXPECT_TRUE(saved == SaveState::Create(*GameBoy));
}

TEST_F(SaveStateSlotsTest, SlotsAreKeptApart)
{
	ASSERT_TRUE(Slots.Save(*GameBoy).Succeeded);
	auto first = SaveState::Create(*GameBoy);
	RunFrames(*GameBoy, 10);
	Slots.NextSlot();
	ASSERT_TRUE(Slots.Save(*GameBoy).Succeeded);

	Slots.PreviousSlot();
	ASSERT_TRUE(Slots.Load(*GameBoy).Succeeded);

	EXPECT_TRUE(first == SaveState::Create(*GameBoy));
}

TEST_F(SaveStateSlotsTest, SavingAgainReplacesTheSlotsState)
{
	ASSERT_TRUE(Slots.Save(*GameBoy).Succeeded);
	RunFrames(*GameBoy, 10);
	ASSERT_TRUE(Slots.Save(*GameBoy).Succeeded);

	EXPECT_TRUE(SaveState::Create(*GameBoy) == ReadAll(Slots.PathForSlot(1)));
}

TEST_F(SaveStateSlotsTest, LoadingAnEmptySlotSaysSo)
{
	Slots.PreviousSlot();

	ExpectLoadFails("No state saved in slot 4");
}

TEST_F(SaveStateSlotsTest, LoadingACorruptStateSaysWhy)
{
	ASSERT_TRUE(Slots.Save(*GameBoy).Succeeded);
	auto state = ReadAll(Slots.PathForSlot(1));
	state[state.size() / 2] ^= 1;
	WriteAll(Slots.PathForSlot(1), state);
	RunFrames(*GameBoy, 5);

	ExpectLoadFails("Couldn't load state 1 from " + Slots.PathForSlot(1) + ". The save state is corrupt");
}

TEST_F(SaveStateSlotsTest, LoadingAnotherGamesStateSaysWhy)
{
	auto other = PowerOn("roms/dmg-acid2/dmg-acid2.gb");
	ASSERT_NE(nullptr, other);
	ASSERT_TRUE(Slots.Save(*other).Succeeded);

	ExpectLoadFails("Couldn't load state 1 from " + Slots.PathForSlot(1) + ". The save state is for another game (DMG-ACID2)");
}

TEST_F(SaveStateSlotsTest, LoadingSomethingElseSaysWhy)
{
	WriteAll(Slots.PathForSlot(1), { 'n', 'o', 't', ' ', 'a', ' ', 's', 't', 'a', 't', 'e' });

	ExpectLoadFails("Couldn't load state 1 from " + Slots.PathForSlot(1) + ". The file isn't an Emu Boy save state");
}

TEST(SaveStateSlotsErrorTest, SavingWhereItCantBeWrittenSaysWhy)
{
	TestDirectory directory{ "SaveStateSlotsTest" };
	SaveStateSlots slots{ directory.Path() + "/missing/game.gb" };
	auto emulator = PowerOn();
	ASSERT_NE(nullptr, emulator);

	auto result = slots.Save(*emulator);

	EXPECT_FALSE(result.Succeeded);
	EXPECT_EQ(0u, result.Message.find("Couldn't save state 1. Couldn't create " + slots.PathForSlot(1) + ".tmp: ")) << result.Message;
}
