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

TEST(CgbSaveStateTest, AnHBlankTransferCarriesOnAfterLoading)
{
	std::vector<unsigned char> data(0x80);
	for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<unsigned char>(0x80 + i);

	CgbTestProgram program;
	program.Data(0x4000, data);
	program.WriteIo(0x51, 0x40).WriteIo(0x52, 0x00).WriteIo(0x53, 0x81).WriteIo(0x54, 0x00).WriteIo(0x55, 0x87).Loop();

	auto emulator = program.PowerOn();
	while (emulator->GetMemoryMap().ReadByte(0xff44) != 3) emulator->RunInstruction();
	auto state = SaveState::Create(*emulator);

	auto restored = program.PowerOn();
	ASSERT_EQ("", SaveState::Restore(*restored, state));

	for (auto i = 0; i < 3; i++)
	{
		emulator->GetFrame();
		restored->GetFrame();
	}

	EXPECT_EQ(0xff, restored->GetMemoryMap().ReadByte(0xff55));
	EXPECT_TRUE(SaveState::Create(*emulator) == SaveState::Create(*restored));

	restored->GetMemoryMap().WriteByte(0xff40, 0x00);
	for (size_t i = 0; i < data.size(); i++) EXPECT_EQ(data[i], restored->GetMemoryMap().ReadByte(static_cast<unsigned short>(0x8100 + i)));
}

TEST(CgbSaveStateTest, AVramDmaCopyWaitingForTheNextInstructionHappensAfterLoading)
{
	// Saved straight after HDMA5 starts a general-purpose transfer, which runs after the next opcode fetch
	std::vector<unsigned char> data(0x10, 0x5a);
	CgbTestProgram program;
	program.Data(0x4000, data);
	program.WriteIo(0x40, 0x00).WriteIo(0x51, 0x40).WriteIo(0x52, 0x00).WriteIo(0x53, 0x80).WriteIo(0x54, 0x00).WriteIo(0x55, 0x00);
	auto started = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, started));
	auto state = SaveState::Create(*emulator);

	auto restored = program.PowerOn();
	ASSERT_EQ("", SaveState::Restore(*restored, state));
	auto before = restored->GetSystemClocks();
	restored->RunInstruction();

	// The NOP, a machine cycle to set up and 8 to copy the block
	EXPECT_EQ(40u, restored->GetSystemClocks() - before);
	EXPECT_EQ(0x5a, restored->GetMemoryMap().ReadByte(0x8000));
}

TEST(CgbSaveStateTest, TheCgbScreenComesBackFromAState)
{
	auto cartridge = CartridgeFactory::LoadFromFile("roms/cgb-acid2/cgb-acid2.gbc");
	ASSERT_NE(nullptr, cartridge);
	Emulator emulator{ cartridge };
	for (auto i = 0; i < 10; i++) emulator.GetFrame();
	auto state = SaveState::Create(emulator);

	Emulator restored{ CartridgeFactory::LoadFromFile("roms/cgb-acid2/cgb-acid2.gbc") };
	ASSERT_EQ("", SaveState::Restore(restored, state));

	auto size = Graphics::HozPixels * Graphics::VertPixels;
	EXPECT_TRUE(std::equal(emulator.GetCgbScreenRgb555(), emulator.GetCgbScreenRgb555() + size, restored.GetCgbScreenRgb555()));
	EXPECT_TRUE(std::equal(emulator.GetScreen(), emulator.GetScreen() + size, restored.GetScreen()));
}

TEST(CgbSaveStateTest, AStateWithACgbColourOutOfRangeIsRejected)
{
	auto emulator = CgbTestProgram().PowerOn();
	auto state = SaveState::Create(*emulator);

	// The frame's pixels follow VRAM, OAM, the registers, 8 values, 11 flags and the frame position
	auto ppu = FindSection(state, "PPU ");
	auto firstPixel = ppu + 0x2000 + 160 + 12 + 8 * 4 + 11 + 4;
	state[firstPixel] = 0x00;
	state[firstPixel + 1] = 0x80;
	FixCrc(state);

	auto error = SaveState::Restore(*emulator, state);
	EXPECT_NE(std::string::npos, error.find("a pixel's colour is 32768")) << error;
}

TEST(CgbSaveStateTest, VersionsOutsideTwoToThreeAreRejected)
{
	auto emulator = CgbTestProgram().PowerOn();
	auto state = SaveState::Create(*emulator);
	state[8] = 1;

	auto error = SaveState::Restore(*emulator, state);
	EXPECT_NE(std::string::npos, error.find("reads versions 2 to 3")) << error;
}
