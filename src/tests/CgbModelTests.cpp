#include "stdafx.h"
#include <gtest/gtest.h>
#include <vector>
#include "CgbTestProgram.h"
#include "../core/GameBoyModel.h"

// Choosing the model from the header's CGB flag (https://gbdev.io/pandocs/The_Cartridge_Header.html#0143--cgb-flag),
// and the state CGB mode starts in, which the CGB boot ROM would have left
// (https://gbdev.io/pandocs/Power_Up_Sequence.html)

namespace
{
	std::vector<unsigned char> RomWithCgbFlag(unsigned char flag)
	{
		std::vector<unsigned char> rom(0x8000);
		rom[0x143] = flag;
		return rom;
	}

	unsigned char Read(Emulator& emulator, unsigned short address) { return emulator.GetMemoryMap().ReadByte(address); }
}

TEST(GameBoyModelTest, CgbFlagsChooseTheCgb)
{
	EXPECT_EQ(GameBoyModel::Cgb, ModelForRom(RomWithCgbFlag(0x80)));
	EXPECT_EQ(GameBoyModel::Cgb, ModelForRom(RomWithCgbFlag(0xc0)));
}

TEST(GameBoyModelTest, AnythingElseChoosesTheDmg)
{
	// $00 is a DMG game, and bit 7 with bits 2 or 3 set selects the CGB's "PGB" mode rather than CGB mode
	for (auto flag : { 0x00, 0x01, 0x40, 0x7f, 0x84, 0x88, 0xc4, 0xc8, 0xff })
	{
		EXPECT_EQ(GameBoyModel::Dmg, ModelForRom(RomWithCgbFlag(static_cast<unsigned char>(flag)))) << "flag " << flag;
	}

	// The last byte of the title on older cartridges: game titles are ASCII, below $80
	EXPECT_EQ(GameBoyModel::Dmg, ModelForRom(RomWithCgbFlag('A')));
}

TEST(GameBoyModelTest, ARomTooShortForAHeaderChoosesTheDmg)
{
	EXPECT_EQ(GameBoyModel::Dmg, ModelForRom(std::vector<unsigned char>(0x143)));
	EXPECT_EQ(GameBoyModel::Dmg, ModelForRom({}));
}

TEST(GameBoyModelTest, TheEmulatorFollowsTheHeaderUnlessTold)
{
	auto cgbRom = CgbTestProgram().Rom();
	EXPECT_EQ(GameBoyModel::Cgb, Emulator(CartridgeFactory::Create(std::vector<unsigned char>(cgbRom))).GetModel());
	EXPECT_EQ(GameBoyModel::Dmg, Emulator(CartridgeFactory::Create(std::vector<unsigned char>(cgbRom)), GameBoyModel::Dmg).GetModel());

	auto dmgRom = RomWithCgbFlag(0x00);
	EXPECT_EQ(GameBoyModel::Dmg, Emulator(CartridgeFactory::Create(std::vector<unsigned char>(dmgRom))).GetModel());
	EXPECT_EQ(GameBoyModel::Cgb, Emulator(CartridgeFactory::Create(std::vector<unsigned char>(dmgRom)), GameBoyModel::Cgb).GetModel());
}

TEST(GameBoyModelTest, TheDmgStartsWithItsBootRom)
{
	auto emulator = CgbTestProgram().PowerOn(GameBoyModel::Dmg);
	EXPECT_EQ(0x0000, emulator->GetCpu().GetRegisters().PC);
	EXPECT_TRUE(emulator->GetMemoryMap().InternalRomEnabled());
}

TEST(CgbPostBootTest, CpuRegistersAreTheCgbBootRomsForACgbGame)
{
	auto emulator = CgbTestProgram().PowerOn();
	auto& registers = emulator->GetCpu().GetRegisters();

	EXPECT_EQ(0x11, registers.A);	// identifies a CGB
	EXPECT_EQ(0x80, registers.F);	// Z set, N, H and C clear
	EXPECT_EQ(0x0000, registers.BC);
	EXPECT_EQ(0xff56, registers.DE);
	EXPECT_EQ(0x000d, registers.HL);
	EXPECT_EQ(0xfffe, registers.SP);
	EXPECT_EQ(0x0100, registers.PC);
}

TEST(CgbPostBootTest, TheBootRomHasHandedOver)
{
	auto emulator = CgbTestProgram().PowerOn();
	EXPECT_FALSE(emulator->GetMemoryMap().InternalRomEnabled());
	EXPECT_EQ(0xc3, Read(*emulator, 0x0100));
}

TEST(CgbPostBootTest, HardwareRegistersHaveTheirDocumentedValues)
{
	auto emulator = CgbTestProgram().PowerOn();

	EXPECT_EQ(0x00, Read(*emulator, 0xff01));	// SB
	EXPECT_EQ(0x7f, Read(*emulator, 0xff02));	// SC
	EXPECT_EQ(0x00, Read(*emulator, 0xff05));	// TIMA
	EXPECT_EQ(0x00, Read(*emulator, 0xff06));	// TMA
	EXPECT_EQ(0xf8, Read(*emulator, 0xff07));	// TAC
	EXPECT_EQ(0x80, Read(*emulator, 0xff10));	// NR10
	EXPECT_EQ(0xbf, Read(*emulator, 0xff11));	// NR11
	EXPECT_EQ(0xf3, Read(*emulator, 0xff12));	// NR12
	EXPECT_EQ(0xff, Read(*emulator, 0xff13));	// NR13
	EXPECT_EQ(0xbf, Read(*emulator, 0xff14));	// NR14
	EXPECT_EQ(0x3f, Read(*emulator, 0xff16));	// NR21
	EXPECT_EQ(0x00, Read(*emulator, 0xff17));	// NR22
	EXPECT_EQ(0xff, Read(*emulator, 0xff18));	// NR23
	EXPECT_EQ(0xbf, Read(*emulator, 0xff19));	// NR24
	EXPECT_EQ(0x7f, Read(*emulator, 0xff1a));	// NR30
	EXPECT_EQ(0xff, Read(*emulator, 0xff1b));	// NR31
	EXPECT_EQ(0x9f, Read(*emulator, 0xff1c));	// NR32
	EXPECT_EQ(0xff, Read(*emulator, 0xff1d));	// NR33
	EXPECT_EQ(0xbf, Read(*emulator, 0xff1e));	// NR34
	EXPECT_EQ(0xff, Read(*emulator, 0xff20));	// NR41
	EXPECT_EQ(0x00, Read(*emulator, 0xff21));	// NR42
	EXPECT_EQ(0x00, Read(*emulator, 0xff22));	// NR43
	EXPECT_EQ(0xbf, Read(*emulator, 0xff23));	// NR44
	EXPECT_EQ(0x77, Read(*emulator, 0xff24));	// NR50
	EXPECT_EQ(0xf3, Read(*emulator, 0xff25));	// NR51
	EXPECT_EQ(0xf1, Read(*emulator, 0xff26));	// NR52: powered, CH1 on
	EXPECT_EQ(0x91, Read(*emulator, 0xff40));	// LCDC
	EXPECT_EQ(0x00, Read(*emulator, 0xff42));	// SCY
	EXPECT_EQ(0x00, Read(*emulator, 0xff43));	// SCX
	EXPECT_EQ(0x00, Read(*emulator, 0xff45));	// LYC
	EXPECT_EQ(0x00, Read(*emulator, 0xff46));	// DMA
	EXPECT_EQ(0xfc, Read(*emulator, 0xff47));	// BGP
	EXPECT_EQ(0x00, Read(*emulator, 0xff4a));	// WY
	EXPECT_EQ(0x00, Read(*emulator, 0xff4b));	// WX
	EXPECT_EQ(0xfe, Read(*emulator, 0xff4f));	// VBK
	EXPECT_EQ(0x2e, Read(*emulator, 0xff56));	// RP: Pan Docs gives $3E, but bit 4 reads 0 on revision E (InfraredPort.h)
	EXPECT_EQ(0xf8, Read(*emulator, 0xff70));	// SVBK
}

TEST(CgbPostBootTest, InterruptRegistersHaveTheirDocumentedValues)
{
	// Read by the program, as the CPU holds IF and IE
	CgbTestProgram program;
	program.ReadIo(0x0f).Op({ 0x47 });	// LD B,A
	program.ReadIo(0xff).Op({ 0x4f });	// LD C,A
	auto end = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));

	auto& registers = emulator->GetCpu().GetRegisters();
	EXPECT_EQ(0xe1, registers.B);	// IF: V-Blank requested
	EXPECT_EQ(0x00, registers.C);	// IE
}

TEST(CgbPostBootTest, WaveRamHoldsTheBootRomsPattern)
{
	auto emulator = CgbTestProgram().PowerOn();
	for (unsigned short address = 0xff30; address < 0xff40; address++)
	{
		EXPECT_EQ(address & 1 ? 0xff : 0x00, Read(*emulator, address)) << std::hex << address;
	}
}

