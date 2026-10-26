#include "stdafx.h"
#include <gtest/gtest.h>
#include "../core/BatteryStore.h"
#include "../core/Emulator.h"
#include "../core/RealTimeClock.h"
#include "SaveFileTestHelpers.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace SaveFileTestHelpers;

namespace
{
	bool MakeDirectory(const std::string& path)
	{
#ifdef _WIN32
		return _mkdir(path.c_str()) == 0;
#else
		return mkdir(path.c_str(), 0700) == 0;
#endif
	}

	void RemoveDirectory(const std::string& path)
	{
#ifdef _WIN32
		_rmdir(path.c_str());
#else
		rmdir(path.c_str());
#endif
	}

	bool Exists(const std::string& path)
	{
		std::ifstream file(path);
		return file.good();
	}

	std::vector<unsigned char> ReadAll(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	void WriteAll(const std::string& path, const std::vector<unsigned char>& data)
	{
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(data.data()), data.size());
	}

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
			auto temp = std::getenv("TMPDIR");
			if (temp == nullptr) temp = std::getenv("TEMP");

			Directory = std::string(temp != nullptr ? temp : ".") + "/EmuBoyBatteryStoreTest-" +
				std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(_nextDirectory++);
			MakeDirectory(Directory);
			SavePath = Directory + "/game.sav";
		}

		~BatteryStoreTest()
		{
			std::remove(SavePath.c_str());
			RemoveDirectory(SavePath);
			RemoveDirectory(SavePath + ".tmp");
			std::remove((SavePath + ".tmp").c_str());
			RemoveDirectory(Directory);
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
	ASSERT_TRUE(MakeDirectory(SavePath + ".tmp"));
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
	ASSERT_TRUE(MakeDirectory(SavePath));
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
	ASSERT_TRUE(MakeDirectory(SavePath));
	auto cartridge = CartridgeFactory::Create(Rom(Mbc1RamBattery, Ram8K));
	auto store = Store(cartridge);

	auto load = store.Load();
	EnableRam(*cartridge);
	cartridge->RamWriteByte(0x0000, 0x99);
	auto periodicSave = store.SaveIfChanged();
	auto save = store.Save();

	EXPECT_FALSE(load.Succeeded);
	EXPECT_NE("", load.Message);
	EXPECT_TRUE(periodicSave.Succeeded);
	EXPECT_FALSE(save.Succeeded);
	EXPECT_NE("", save.Message);
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
