#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/BatteryStore.h"
#include "../core/Emulator.h"
#include "../core/RealTimeClock.h"
#include "SaveFileTestHelpers.h"
#include "TestFiles.h"
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <string>

using namespace SaveFileTestHelpers;
using namespace TestFiles;

namespace
{
	// Each test gets its own save path in a fresh directory, removed afterwards
	class BatteryStoreTest : public testing::Test
	{
		static int _nextDirectory;

	public:
		std::string Directory;
		std::string SavePath;
		int64_t Now{ 1700000000 };
		BatteryStore::TimeSource Clock{ [this] { return Now; } };

		BatteryStoreTest()
		{
			Directory = TempDirectory() + "/EmuBoyBatteryStoreTest-" +
				std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(_nextDirectory++);
			MakeTestDirectory(Directory);
			SavePath = Directory + "/game.sav";
		}

		~BatteryStoreTest()
		{
			for (auto suffix : { "", ".tmp", ".bak", ".bak.tmp", ".before-state", ".before-state.tmp" })
			{
				auto path = SavePath + suffix;
				std::remove(path.c_str());
				RemoveTestDirectory(path);
			}

			RemoveTestDirectory(Directory);
		}

		BatteryStore Store(std::shared_ptr<Cartridge> cartridge)
		{
			return BatteryStore(cartridge, SavePath, Clock);
		}
	};

	int BatteryStoreTest::_nextDirectory = 0;
}

TEST(BatteryStorePathTest, ReplacesRomExtensionWithSav)
{
	EXPECT_EQ("roms/Tetris.sav", BatteryStore::SavePathForRom("roms/Tetris.gb"));
	EXPECT_EQ("C:\\games\\Zelda.sav", BatteryStore::SavePathForRom("C:\\games\\Zelda.gbc"));
	EXPECT_EQ("Game.v1.sav", BatteryStore::SavePathForRom("Game.v1.gb"));
}

TEST(BatteryStorePathTest, AddsSavWithoutExtension)
{
	EXPECT_EQ("roms/Tetris.sav", BatteryStore::SavePathForRom("roms/Tetris"));
	EXPECT_EQ("my.roms/Tetris.sav", BatteryStore::SavePathForRom("my.roms/Tetris"));
}

TEST_F(BatteryStoreTest, MissingSaveLeavesRamBlankAndCreatesNothing)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	auto result = Store(cartridge).Load();

	EXPECT_TRUE(result.Succeeded);
	EXPECT_EQ("", result.Message);
	EXPECT_EQ(std::vector<unsigned char>(Ram8KSize), cartridge->GetRam());
	EXPECT_FALSE(Exists(SavePath));
}

TEST_F(BatteryStoreTest, SavedRamLoadsIntoTheNextRun)
{
	auto original = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram32K));
	FillRam(*original, 11);
	ASSERT_TRUE(Store(original).Save().Succeeded);

	auto restored = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram32K));
	auto result = Store(restored).Load();

	EXPECT_TRUE(result.Succeeded);
	EXPECT_EQ("", result.Message);
	EXPECT_EQ(original->GetRam(), restored->GetRam());
	EXPECT_EQ(original->GetRam(), ReadAll(SavePath));
}

TEST_F(BatteryStoreTest, TwoKbRamSaveIs2048Bytes)
{
	auto original = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	FillRam(*original, 5);
	ASSERT_TRUE(Store(original).Save().Succeeded);

	auto restored = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	auto result = Store(restored).Load();

	EXPECT_EQ(Ram2KSize, ReadAll(SavePath).size());
	EXPECT_EQ("", result.Message);
	EXPECT_EQ(original->GetRam(), restored->GetRam());
}

TEST_F(BatteryStoreTest, BatteryCartridgesWhoseMapperIsNotEmulatedKeepTheirSaves)
{
	// MMM01+RAM+BATTERY, Pocket Camera, HuC3 and HuC1+RAM+BATTERY run as MBC1, but their RAM is still saved
	for (auto type : { Mmm01RamBattery, PocketCamera, HuC3, HuC1RamBattery })
	{
		SCOPED_TRACE(static_cast<int>(type));
		std::remove(SavePath.c_str());

		auto original = CartridgeFactory::Create(Rom(type, Ram32K));
		FillRam(*original, type);
		ASSERT_TRUE(Store(original).IsNeeded());
		ASSERT_TRUE(Store(original).Save().Succeeded);

		auto restored = CartridgeFactory::Create(Rom(type, Ram32K));
		EXPECT_EQ("", Store(restored).Load().Message);
		EXPECT_EQ(original->GetRam(), restored->GetRam());
	}
}

TEST_F(BatteryStoreTest, SaveLeavesNoTemporaryFile)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	Store(cartridge).Save();

	EXPECT_TRUE(Exists(SavePath));
	EXPECT_FALSE(Exists(SavePath + ".tmp"));
}

TEST_F(BatteryStoreTest, SaveReplacesExistingSaveAndStaleTemporaryFile)
{
	WriteAll(SavePath, std::vector<unsigned char>(Ram8KSize, 0x11));
	WriteAll(SavePath + ".tmp", std::vector<unsigned char>(5, 0x22));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	FillRam(*cartridge, 3);

	EXPECT_TRUE(Store(cartridge).Save().Succeeded);

	EXPECT_EQ(cartridge->GetRam(), ReadAll(SavePath));
	EXPECT_FALSE(Exists(SavePath + ".tmp"));
}

TEST_F(BatteryStoreTest, FailedWriteLeavesExistingSaveIntact)
{
	// A directory where the temporary file should go stops it being written
	std::vector<unsigned char> existing(Ram8KSize, 0x11);
	WriteAll(SavePath, existing);
	ASSERT_TRUE(MakeTestDirectory(SavePath + ".tmp"));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	FillRam(*cartridge, 3);
	cartridge->MarkSaved();
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0x0000, 0x99);

	auto result = Store(cartridge).Save();

	EXPECT_FALSE(result.Succeeded);
	EXPECT_NE("", result.Message);
	EXPECT_EQ(existing, ReadAll(SavePath));
	EXPECT_TRUE(cartridge->HasUnsavedChanges());
}

TEST_F(BatteryStoreTest, FailedRenameRemovesTemporaryFile)
{
	// The save path being a directory stops the temporary file replacing it
	ASSERT_TRUE(MakeTestDirectory(SavePath));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	auto result = Store(cartridge).Save();

	EXPECT_FALSE(result.Succeeded);
	EXPECT_NE("", result.Message);
	EXPECT_FALSE(Exists(SavePath + ".tmp"));
}

TEST_F(BatteryStoreTest, SaveIntoMissingDirectoryReportsError)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	auto result = BatteryStore(cartridge, Directory + "/missing/game.sav", Clock).Save();

	EXPECT_FALSE(result.Succeeded);
	EXPECT_NE("", result.Message);
}

TEST_F(BatteryStoreTest, UnreadableSaveIsReportedAndNotOverwritten)
{
	// A directory can't be read as a save
	ASSERT_TRUE(MakeTestDirectory(SavePath));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);

	auto load = store.Load();
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0x0000, 0x99);
	auto save = store.SaveIfChanged();

	EXPECT_FALSE(load.Succeeded);
	EXPECT_NE("", load.Message);
	EXPECT_FALSE(save.Succeeded);
	EXPECT_NE("", save.Message);
	EXPECT_FALSE(store.SaveOnExit().Succeeded);
	EXPECT_FALSE(Exists(SavePath + ".tmp"));
}

TEST_F(BatteryStoreTest, WrongSizeSaveLoadsWithWarning)
{
	WriteAll(SavePath, std::vector<unsigned char>(100, 0x33));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));

	auto result = Store(cartridge).Load();

	EXPECT_TRUE(result.Succeeded);
	EXPECT_NE(std::string::npos, result.Message.find(SavePath));
	EXPECT_EQ(0x33, cartridge->GetRam()[99]);
	EXPECT_EQ(0x00, cartridge->GetRam()[100]);
}

TEST_F(BatteryStoreTest, CartridgeWithoutBatteryNeverTouchesTheFile)
{
	std::vector<unsigned char> existing(Ram8KSize, 0x44);
	WriteAll(SavePath, existing);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1Ram, Ram8K));
	auto store = Store(cartridge);

	EXPECT_FALSE(store.IsNeeded());
	EXPECT_TRUE(store.Load().Succeeded);
	EXPECT_EQ(std::vector<unsigned char>(Ram8KSize), cartridge->GetRam());

	FillRam(*cartridge, 5);
	EXPECT_TRUE(store.Save().Succeeded);
	EXPECT_EQ(existing, ReadAll(SavePath));
}

TEST_F(BatteryStoreTest, CartridgeWithoutBatteryCreatesNoSave)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1Ram, Ram8K));

	Store(cartridge).Save();

	EXPECT_FALSE(Exists(SavePath));
}

TEST_F(BatteryStoreTest, BatteryWithNothingToKeepCreatesNoSave)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, NoRam));
	auto store = Store(cartridge);

	store.Save();

	EXPECT_FALSE(store.IsNeeded());
	EXPECT_FALSE(Exists(SavePath));
}

TEST_F(BatteryStoreTest, SaveIfChangedOnlyWritesAfterChanges)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	EnableRam(*cartridge);

	EXPECT_TRUE(store.SaveIfChanged().Succeeded);
	EXPECT_FALSE(Exists(SavePath));

	cartridge->RamWriteByte(0x0123, 0x45);
	EXPECT_TRUE(store.SaveIfChanged().Succeeded);
	EXPECT_EQ(0x45, ReadAll(SavePath)[0x0123]);
	EXPECT_FALSE(cartridge->HasUnsavedChanges());
}

TEST_F(BatteryStoreTest, SaveIfChangedSkipsUnchangedRamAfterLoad)
{
	WriteAll(SavePath, std::vector<unsigned char>(Ram8KSize, 0x55));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();
	std::remove(SavePath.c_str());

	store.SaveIfChanged();

	EXPECT_FALSE(Exists(SavePath));
}

TEST_F(BatteryStoreTest, ClockCatchesUpWithTimeBetweenRuns)
{
	auto original = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram8K));
	RealTimeClock::State state;
	state.Counters = { { 10, 20, 3, 0, 0 } };
	original->GetRealTimeClock()->SetState(state);
	Store(original).Save();

	Now += 2 * 60 * 60 + 5;
	auto restored = CartridgeFactory::Create(Rom(Mbc3TimerRamBattery, Ram8K));
	Store(restored).Load();

	EXPECT_EQ((std::array<unsigned char, RealTimeClock::RegisterCount>{ { 15, 20, 5, 0, 0 } }),
		restored->GetRealTimeClock()->GetState().Counters);
	EXPECT_EQ(Ram8KSize + 48, ReadAll(SavePath).size());
}

TEST_F(BatteryStoreTest, GameReadsSavedRamThroughTheEmulator)
{
	auto rom = Rom(Mbc5RamBattery, Ram32K);
	{
		auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
		Emulator emulator{ cartridge };
		auto& memory = emulator.GetMemoryMap();
		memory.WriteByte(0x0000, 0x0a);
		memory.WriteByte(0x4000, 0x02);
		memory.WriteByte(0xa456, 0x78);
		ASSERT_TRUE(Store(cartridge).SaveIfChanged().Succeeded);
	}

	auto cartridge = CartridgeFactory::Create(std::move(rom));
	Store(cartridge).Load();
	Emulator emulator{ cartridge };
	auto& memory = emulator.GetMemoryMap();
	memory.WriteByte(0x0000, 0x0a);
	memory.WriteByte(0x4000, 0x02);

	EXPECT_EQ(0x78, memory.ReadByte(0xa456));
}

TEST_F(BatteryStoreTest, RepeatedErrorIsReportedOnceUntilASaveSucceeds)
{
	ASSERT_TRUE(MakeTestDirectory(SavePath));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);

	auto first = store.Save();
	auto second = store.Save();
	RemoveTestDirectory(SavePath);
	auto success = store.Save();
	std::remove(SavePath.c_str());
	ASSERT_TRUE(MakeTestDirectory(SavePath));
	auto again = store.Save();

	EXPECT_FALSE(first.Succeeded);
	EXPECT_NE("", first.Message);
	EXPECT_FALSE(second.Succeeded);
	EXPECT_EQ("", second.Message);
	EXPECT_TRUE(success.Succeeded);
	EXPECT_FALSE(again.Succeeded);
	EXPECT_EQ(first.Message, again.Message);
}

TEST_F(BatteryStoreTest, WrongSizeSaveIsBackedUpBeforeBeingReplaced)
{
	std::vector<unsigned char> original(100, 0x33);
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	auto load = store.Load();

	EXPECT_TRUE(store.Save().Succeeded);

	EXPECT_NE(std::string::npos, load.Message.find(".bak"));
	EXPECT_EQ(original, ReadAll(SavePath + ".bak"));
	EXPECT_EQ(Ram8KSize, ReadAll(SavePath).size());
}

TEST_F(BatteryStoreTest, MgbaEightKbSaveForTwoKbRamIsBackedUp)
{
	// It loads without a warning, but its upper 6 KB, which may hold data a game wrote under mGBA, isn't kept in the
	// 2 KB save that replaces it
	std::vector<unsigned char> original(Ram8KSize, 0x11);
	std::fill(original.begin() + Ram2KSize, original.end(), 0x77);
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram2K));
	auto store = Store(cartridge);
	auto load = store.Load();

	EXPECT_TRUE(store.Save().Succeeded);

	EXPECT_TRUE(load.Succeeded);
	EXPECT_NE(std::string::npos, load.Message.find(".bak"));
	EXPECT_EQ(std::vector<unsigned char>(Ram2KSize, 0x11), cartridge->GetRam());
	EXPECT_EQ(original, ReadAll(SavePath + ".bak"));
	EXPECT_EQ(Ram2KSize, ReadAll(SavePath).size());
}

TEST_F(BatteryStoreTest, MgbaPackedMbc2SaveIsBackedUp)
{
	// mGBA's 256-byte MBC2 save loads without a warning, but is replaced by a 512-byte one
	std::vector<unsigned char> original(256, 0x21);
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc2Battery, NoRam));
	auto store = Store(cartridge);
	auto load = store.Load();

	EXPECT_TRUE(store.Save().Succeeded);

	EXPECT_NE(std::string::npos, load.Message.find(".bak"));
	EXPECT_EQ(original, ReadAll(SavePath + ".bak"));
	EXPECT_EQ(512u, ReadAll(SavePath).size());
}

TEST_F(BatteryStoreTest, LegacyClockFooterSaveIsBackedUp)
{
	// Older VBA versions' 44-byte footer (with a 32-bit time) loads without a warning, but is replaced by a 48-byte one
	std::vector<unsigned char> original(44, 0x00);
	original[0] = 0x05;
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));
	auto store = Store(cartridge);
	auto load = store.Load();

	EXPECT_TRUE(store.Save().Succeeded);

	EXPECT_EQ(5, cartridge->GetRealTimeClock()->GetState().Counters[0]);
	EXPECT_NE(std::string::npos, load.Message.find(".bak"));
	EXPECT_EQ(original, ReadAll(SavePath + ".bak"));
	EXPECT_EQ(48u, ReadAll(SavePath).size());
}

TEST_F(BatteryStoreTest, WrongSizeSaveIsBackedUpOnlyOnce)
{
	WriteAll(SavePath, std::vector<unsigned char>(100, 0x33));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();
	store.Save();
	std::remove((SavePath + ".bak").c_str());

	store.Save();

	EXPECT_FALSE(Exists(SavePath + ".bak"));
}

TEST_F(BatteryStoreTest, RightSizeSaveIsNotBackedUp)
{
	WriteAll(SavePath, std::vector<unsigned char>(Ram8KSize, 0x33));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();

	store.Save();

	EXPECT_FALSE(Exists(SavePath + ".bak"));
}

TEST_F(BatteryStoreTest, FailedBackupLeavesWrongSizeSaveInPlace)
{
	// A directory where the backup's temporary file should go stops it being written
	std::vector<unsigned char> original(100, 0x33);
	WriteAll(SavePath, original);
	ASSERT_TRUE(MakeTestDirectory(SavePath + ".bak.tmp"));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();

	auto result = store.Save();

	EXPECT_FALSE(result.Succeeded);
	EXPECT_NE("", result.Message);
	EXPECT_EQ(original, ReadAll(SavePath));
}

TEST_F(BatteryStoreTest, ExitWithoutChangesLeavesTheSaveAlone)
{
	std::vector<unsigned char> original(100, 0x33);
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();

	EXPECT_TRUE(store.SaveOnExit().Succeeded);

	EXPECT_EQ(original, ReadAll(SavePath));
	EXPECT_FALSE(Exists(SavePath + ".bak"));
}

TEST_F(BatteryStoreTest, ExitWithoutChangesOrSaveCreatesNoSave)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();

	store.SaveOnExit();

	EXPECT_FALSE(Exists(SavePath));
}

TEST_F(BatteryStoreTest, ExitSavesChanges)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0x0010, 0x66);

	EXPECT_TRUE(store.SaveOnExit().Succeeded);

	EXPECT_EQ(0x66, ReadAll(SavePath)[0x0010]);
}

TEST_F(BatteryStoreTest, ExitAlwaysSavesClockCartridgeWithCurrentTime)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc3TimerBattery, NoRam));
	auto store = Store(cartridge);
	store.Save();
	Now += 1000;

	EXPECT_TRUE(store.SaveOnExit().Succeeded);

	auto save = ReadAll(SavePath);
	ASSERT_EQ(48u, save.size());
	int64_t timeStamp = 0;
	for (auto i = 0; i < 8; i++) timeStamp |= static_cast<int64_t>(save[40 + i]) << (8 * i);
	EXPECT_EQ(Now, timeStamp);
}

TEST_F(BatteryStoreTest, BeforeAStateLoadsTheSaveIsCopiedAside)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	FillRam(*cartridge, 3);
	ASSERT_TRUE(store.Save().Succeeded);

	EXPECT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);

	EXPECT_EQ(SavePath + ".before-state", store.GetStateLoadCopyPath());
	EXPECT_EQ(cartridge->GetRam(), ReadAll(store.GetStateLoadCopyPath()));
	EXPECT_FALSE(Exists(SavePath + ".bak"));
}

TEST_F(BatteryStoreTest, RamChangedJustBeforeAStateLoadsIsSavedAndCopied)
{
	// The game saved since the last periodic save, so the change is only in memory
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	FillRam(*cartridge, 3);
	ASSERT_TRUE(store.Save().Succeeded);
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0, 0x22);

	EXPECT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);

	EXPECT_EQ(0x22, ReadAll(SavePath)[0]);
	EXPECT_EQ(0x22, ReadAll(store.GetStateLoadCopyPath())[0]);
	EXPECT_FALSE(cartridge->HasUnsavedChanges());
}

TEST_F(BatteryStoreTest, EachStateLoadRefreshesTheCopy)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	FillRam(*cartridge, 3);
	ASSERT_TRUE(store.Save().Succeeded);
	ASSERT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);

	FillRam(*cartridge, 5);
	ASSERT_TRUE(store.Save().Succeeded);
	EXPECT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);

	EXPECT_EQ(cartridge->GetRam(), ReadAll(store.GetStateLoadCopyPath()));
}

TEST_F(BatteryStoreTest, StateLoadLeavesAWrongSizeSavesBackupAlone)
{
	std::vector<unsigned char> original(100, 0x33);
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();
	ASSERT_TRUE(store.Save().Succeeded);
	ASSERT_EQ(original, ReadAll(SavePath + ".bak"));

	FillRam(*cartridge, 5);
	EXPECT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);
	FillRam(*cartridge, 7);
	ASSERT_TRUE(store.Save().Succeeded);

	EXPECT_EQ(original, ReadAll(SavePath + ".bak"));
}

TEST_F(BatteryStoreTest, WrongSizeSaveIsBackedUpWhenItsFirstReplacedBeforeAStateLoads)
{
	std::vector<unsigned char> original(100, 0x33);
	WriteAll(SavePath, original);
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	store.Load();
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0, 0x44);

	EXPECT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);

	EXPECT_EQ(original, ReadAll(SavePath + ".bak"));
	EXPECT_EQ(cartridge->GetRam(), ReadAll(store.GetStateLoadCopyPath()));
}

TEST_F(BatteryStoreTest, WithoutASaveThereIsNoCopy)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	WriteAll(store.GetStateLoadCopyPath(), { 1, 2, 3 });

	EXPECT_TRUE(store.KeepSaveBeforeStateLoad().Succeeded);

	EXPECT_FALSE(Exists(SavePath));
	EXPECT_FALSE(Exists(store.GetStateLoadCopyPath()));
}

TEST_F(BatteryStoreTest, FailingToSaveBeforeAStateLoadsSaysWhy)
{
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0, 0x44);
	ASSERT_TRUE(MakeTestDirectory(SavePath + ".tmp"));

	auto result = store.KeepSaveBeforeStateLoad();

	EXPECT_FALSE(result.Succeeded);
	EXPECT_NE(std::string::npos, result.Message.find(SavePath)) << result.Message;
	EXPECT_FALSE(Exists(store.GetStateLoadCopyPath()));
}
