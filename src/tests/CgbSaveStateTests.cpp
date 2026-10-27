#include "stdafx.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "CgbTestProgram.h"
#include "../core/SaveState.h"
#include "../core/StateStream.h"
#include "../core/Crc32.h"

// Save states in version 3 of the format, which holds the Game Boy Color's state, and loading version 2 states from
// before then. SaveStateRoundTripTests.cpp checks CGB states round-trip while running the CGB test ROMs.

namespace
{
	using Bytes = std::vector<unsigned char>;

	Bytes ReadFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	size_t FindSection(const Bytes& state, const std::string& tag)
	{
		const size_t headerSize = 12;
		for (auto offset = headerSize; offset + 8 <= state.size();)
		{
			auto length = state[offset + 4] | state[offset + 5] << 8 | state[offset + 6] << 16 | static_cast<size_t>(state[offset + 7]) << 24;
			if (std::string(state.begin() + offset, state.begin() + offset + 4) == tag) return offset + 8;
			offset += 8 + length;
		}

		ADD_FAILURE() << "No " << tag << " section";
		return 0;
	}

	void FixCrc(Bytes& state)
	{
		auto bodySize = state.size() - 4;
		auto crc = Crc32::Compute(state.data(), bodySize);
		for (auto i = 0; i < 4; i++) state[bodySize + i] = static_cast<unsigned char>(crc >> (8 * i));
	}
}

TEST(CgbSaveStateTest, AVersion2StateFromBeforeTheCgbLoadsOnTheDmg)
{
	// Saved by Emu Boy before version 3, from cpu_instrs' 01-special, 27 frames after the DMG boot ROM handed over.
	// Its header asks for the CGB, so the emulator starts as one, and loading the state makes it a DMG again
	auto state = ReadFile("states/01-special-v2.ss");
	ASSERT_FALSE(state.empty()) << "State not found; run the tests from src/tests";
	ASSERT_EQ(2, state[8]);

	auto cartridge = CartridgeFactory::LoadFromFileWithRamSize("roms/cpu_instrs/01-special.gb", Cartridge::RamBankSize);
	ASSERT_NE(nullptr, cartridge);
	Emulator emulator{ cartridge };
	ASSERT_EQ(GameBoyModel::Cgb, emulator.GetModel());

	ASSERT_EQ("", SaveState::Restore(emulator, state));
	EXPECT_EQ(GameBoyModel::Dmg, emulator.GetModel());
	EXPECT_EQ(0xff, emulator.GetMemoryMap().ReadByte(0xff4f)) << "The DMG has no VBK";

	// It carries on to pass, as it would have
	std::string serial;
	emulator.GetSerial().SetByteSentHandler([&serial](unsigned char byte) { serial += static_cast<char>(byte); });
	for (auto frame = 0; frame < 60 * 30 && serial.find("Passed") == std::string::npos; frame++) emulator.GetFrame();
	EXPECT_NE(std::string::npos, serial.find("Passed")) << serial;
}

TEST(CgbSaveStateTest, ADmgStateIsAVersion2StateWithTheModelSectionAdded)
{
	// Restored from the version 2 fixture and saved again, then with the model section taken out and the version set
	// back to 2, the state is the fixture, byte for byte
	auto fixture = ReadFile("states/01-special-v2.ss");
	ASSERT_FALSE(fixture.empty()) << "State not found; run the tests from src/tests";

	Emulator emulator{ CartridgeFactory::LoadFromFileWithRamSize("roms/cpu_instrs/01-special.gb", Cartridge::RamBankSize) };
	ASSERT_EQ("", SaveState::Restore(emulator, fixture));
	auto state = SaveState::Create(emulator);
	EXPECT_EQ(3, state[8]);

	auto model = FindSection(state, "MODL") - 8;
	state.erase(state.begin() + model, state.begin() + model + 8 + 4);
	state[8] = 2;
	FixCrc(state);
	EXPECT_TRUE(state == fixture);
}

TEST(CgbSaveStateTest, AStateSaysWhichModelItsFor)
{
	auto emulator = CgbTestProgram().PowerOn();
	auto state = SaveState::Create(*emulator);
	EXPECT_EQ(static_cast<int>(GameBoyModel::Cgb), state[FindSection(state, "MODL")]);

	auto dmg = CgbTestProgram().PowerOn(GameBoyModel::Dmg);
	auto dmgState = SaveState::Create(*dmg);
	EXPECT_EQ(static_cast<int>(GameBoyModel::Dmg), dmgState[FindSection(dmgState, "MODL")]);
}

TEST(CgbSaveStateTest, LoadingAStateSwitchesTheEmulatorToItsModel)
{
	CgbTestProgram program;
	program.WriteIo(0x70, 0x05).WriteIo(0x4f, 0x01).Loop();

	auto cgb = program.PowerOn();
	for (auto i = 0; i < 20; i++) cgb->RunInstruction();
	auto cgbState = SaveState::Create(*cgb);

	auto dmg = program.PowerOn(GameBoyModel::Dmg);
	auto dmgState = SaveState::Create(*dmg);

	ASSERT_EQ("", SaveState::Restore(*dmg, cgbState));
	EXPECT_EQ(GameBoyModel::Cgb, dmg->GetModel());
	EXPECT_EQ(0xfd, dmg->GetMemoryMap().ReadByte(0xff70));
	EXPECT_EQ(0xff, dmg->GetMemoryMap().ReadByte(0xff4f));
	EXPECT_TRUE(cgbState == SaveState::Create(*dmg));

	ASSERT_EQ("", SaveState::Restore(*dmg, dmgState));
	EXPECT_EQ(GameBoyModel::Dmg, dmg->GetModel());
	EXPECT_EQ(0xff, dmg->GetMemoryMap().ReadByte(0xff70));
	EXPECT_TRUE(dmgState == SaveState::Create(*dmg));
}

TEST(CgbSaveStateTest, VersionsOutsideTwoToThreeAreRejected)
{
	auto emulator = CgbTestProgram().PowerOn();
	auto state = SaveState::Create(*emulator);
	state[8] = 1;

	auto error = SaveState::Restore(*emulator, state);
	EXPECT_NE(std::string::npos, error.find("reads versions 2 to 3")) << error;
}
