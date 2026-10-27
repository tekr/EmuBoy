#include "stdafx.h"
#include <gtest/gtest.h>
#include <memory>
#include "CgbTestProgram.h"

// The CGB's memory banks and minor registers, per https://gbdev.io/pandocs/CGB_Registers.html and
// https://gbdev.io/pandocs/Memory_Map.html, through the memory map as the CPU sees it

namespace
{
	const unsigned short Svbk = 0xff70;
	const unsigned short Vbk = 0xff4f;

	class CgbMemoryTest : public testing::Test
	{
	public:
		std::unique_ptr<Emulator> GameBoy{ CgbTestProgram().PowerOn() };
		MemoryMap& Memory{ GameBoy->GetMemoryMap() };

		unsigned char Read(unsigned short address) { return Memory.ReadByte(address); }
		void Write(unsigned short address, unsigned char value) { Memory.WriteByte(address, value); }
	};

	class DmgMemoryTest : public CgbMemoryTest
	{
	public:
		DmgMemoryTest() { GameBoy = CgbTestProgram().PowerOn(GameBoyModel::Dmg); }
		MemoryMap& DmgMemory() { return GameBoy->GetMemoryMap(); }
	};
}

TEST_F(CgbMemoryTest, WorkRamBankSelectsWhatD000Shows)
{
	for (unsigned char bank = 1; bank < 8; bank++)
	{
		Write(Svbk, bank);
		Write(0xd000, bank);
		Write(0xdfff, static_cast<unsigned char>(0x10 + bank));
	}

	for (unsigned char bank = 1; bank < 8; bank++)
	{
		Write(Svbk, bank);
		EXPECT_EQ(bank, Read(0xd000)) << "bank " << static_cast<int>(bank);
		EXPECT_EQ(0x10 + bank, Read(0xdfff)) << "bank " << static_cast<int>(bank);
	}
}

TEST_F(CgbMemoryTest, WorkRamBank0IsAlwaysAtC000)
{
	Write(0xc123, 0x5a);
	for (unsigned char bank = 1; bank < 8; bank++)
	{
		Write(Svbk, bank);
		EXPECT_EQ(0x5a, Read(0xc123));
	}
}

TEST_F(CgbMemoryTest, SelectingBank0SelectsBank1)
{
	Write(Svbk, 1);
	Write(0xd000, 0x11);
	Write(Svbk, 2);
	Write(0xd000, 0x22);

	Write(Svbk, 0);
	EXPECT_EQ(0x11, Read(0xd000));
}

TEST_F(CgbMemoryTest, SvbkReadsItsBankBitsWithTheOthersSet)
{
	EXPECT_EQ(0xf8, Read(Svbk));
	Write(Svbk, 0x05);
	EXPECT_EQ(0xfd, Read(Svbk));

	// Only bits 0-2 select the bank: $0B selects bank 3
	Write(Svbk, 3);
	Write(0xd000, 0x33);
	Write(Svbk, 0x0b);
	EXPECT_EQ(0xfb, Read(Svbk));
	EXPECT_EQ(0x33, Read(0xd000));

	// 0 reads back as 0, though it selects bank 1
	Write(Svbk, 0);
	EXPECT_EQ(0xf8, Read(Svbk));
}

TEST_F(CgbMemoryTest, EchoRamShowsTheSelectedBank)
{
	Write(Svbk, 4);
	Write(0xd456, 0x44);
	EXPECT_EQ(0x44, Read(0xf456));

	Write(0xf457, 0x45);
	EXPECT_EQ(0x45, Read(0xd457));
}

TEST_F(CgbMemoryTest, VramBankSelectsWhat8000Shows)
{
	Write(Vbk, 0);
	Write(0x8000, 0xa0);
	Write(0x9fff, 0xa1);
	Write(Vbk, 1);
	Write(0x8000, 0xb0);
	Write(0x9fff, 0xb1);

	EXPECT_EQ(0xb0, Read(0x8000));
	EXPECT_EQ(0xb1, Read(0x9fff));
	Write(Vbk, 0);
	EXPECT_EQ(0xa0, Read(0x8000));
	EXPECT_EQ(0xa1, Read(0x9fff));
}

TEST_F(CgbMemoryTest, VbkReadsItsBankBitWithTheOthersSet)
{
	EXPECT_EQ(0xfe, Read(Vbk));
	Write(Vbk, 1);
	EXPECT_EQ(0xff, Read(Vbk));

	// Only bit 0 matters
	Write(Vbk, 0xfe);
	EXPECT_EQ(0xfe, Read(Vbk));
	Write(Vbk, 0x03);
	EXPECT_EQ(0xff, Read(Vbk));
}

TEST_F(CgbMemoryTest, UnusableAreaRepeatsTheHighNibbleOfTheAddress)
{
	// CGB revision E: $FEAx reads $AA, $FEBx $BB and so on, while OAM isn't blocked. Turned off, the LCD blocks
	// nothing
	Write(0xff40, 0x00);
	EXPECT_EQ(0xaa, Read(0xfea0));
	EXPECT_EQ(0xaa, Read(0xfeaf));
	EXPECT_EQ(0xbb, Read(0xfeb3));
	EXPECT_EQ(0xee, Read(0xfee9));
	EXPECT_EQ(0xff, Read(0xfeff));

	// Writes change nothing
	Write(0xfec4, 0x12);
	EXPECT_EQ(0xcc, Read(0xfec4));
}

TEST_F(CgbMemoryTest, Key0IsLockedAfterTheBootRom)
{
	EXPECT_EQ(0xff, Read(0xff4c));
	Write(0xff4c, 0x04);
	EXPECT_EQ(0xff, Read(0xff4c));

	// Writing $04 (DMG compatibility mode) after the boot ROM changes nothing
	Write(Vbk, 1);
	EXPECT_EQ(0xff, Read(Vbk));
}

TEST_F(CgbMemoryTest, InfraredPortKeepsItsWritableBits)
{
	// Bits 0 (LED) and 6-7 (read enable) are read/write; 2, 3 and 5 read 1, and bit 4 reads 0 on CGB revision E
	// (SameBoy's memory.c); bit 1 reads 1 as no light is received
	EXPECT_EQ(0x2e, Read(0xff56));
	Write(0xff56, 0x01);
	EXPECT_EQ(0x2f, Read(0xff56));
	Write(0xff56, 0xc0);
	EXPECT_EQ(0xee, Read(0xff56));
	Write(0xff56, 0x00);
	EXPECT_EQ(0x2e, Read(0xff56));

	// Even with reading enabled and the LED on, nothing is received
	Write(0xff56, 0xc1);
	EXPECT_EQ(0xef, Read(0xff56));
}

TEST_F(CgbMemoryTest, UndocumentedRegistersFf72ToFf74AreReadWrite)
{
	for (unsigned short address = 0xff72; address <= 0xff74; address++)
	{
		EXPECT_EQ(0x00, Read(address)) << std::hex << address;
		Write(address, 0xa5);
		EXPECT_EQ(0xa5, Read(address)) << std::hex << address;
		Write(address, 0x5a);
		EXPECT_EQ(0x5a, Read(address)) << std::hex << address;
	}
}

TEST_F(CgbMemoryTest, UndocumentedRegisterFf75KeepsOnlyBits4To6)
{
	EXPECT_EQ(0x8f, Read(0xff75));
	Write(0xff75, 0xff);
	EXPECT_EQ(0xff, Read(0xff75));
	Write(0xff75, 0x50);
	EXPECT_EQ(0xdf, Read(0xff75));
	Write(0xff75, 0x8f);
	EXPECT_EQ(0x8f, Read(0xff75));
}

TEST_F(CgbMemoryTest, SerialControlKeepsTheClockSpeedBit)
{
	EXPECT_EQ(0x7f, Read(0xff02));
	Write(0xff02, 0x00);
	EXPECT_EQ(0x7c, Read(0xff02));
	Write(0xff02, 0x02);
	EXPECT_EQ(0x7e, Read(0xff02));
}

TEST_F(CgbMemoryTest, OamDmaFromE000AndAboveCopiesFf)
{
	// On the CGB the sources from $E000 up don't echo work RAM (SameBoy)
	Write(0xc000, 0x12);
	Write(0xff46, 0xe0);
	for (auto i = 0; i < 200; i++) GameBoy->RunInstruction();

	Write(0xff40, 0x00);
	EXPECT_EQ(0xff, Read(0xfe00));
}

TEST_F(CgbMemoryTest, OamDmaReadsTheSelectedWorkRamBank)
{
	Write(Svbk, 6);
	Write(0xd000, 0x66);
	Write(Svbk, 1);
	Write(0xd000, 0x11);

	Write(Svbk, 6);
	Write(0xff46, 0xd0);
	for (auto i = 0; i < 200; i++) GameBoy->RunInstruction();

	Write(0xff40, 0x00);
	EXPECT_EQ(0x66, Read(0xfe00));
}

TEST_F(DmgMemoryTest, CgbRegistersReadFfAndIgnoreWrites)
{
	for (unsigned short address : { 0xff4c, 0xff4f, 0xff51, 0xff52, 0xff53, 0xff54, 0xff55, 0xff56, 0xff68, 0xff69,
		0xff6a, 0xff6b, 0xff6c, 0xff70, 0xff72, 0xff73, 0xff74, 0xff75, 0xff76, 0xff77 })
	{
		DmgMemory().WriteByte(address, 0x00);
		EXPECT_EQ(0xff, DmgMemory().ReadByte(address)) << std::hex << address;
	}
}

TEST_F(DmgMemoryTest, WorkRamIsntBanked)
{
	DmgMemory().WriteByte(0xd000, 0x12);
	DmgMemory().WriteByte(Svbk, 3);
	EXPECT_EQ(0x12, DmgMemory().ReadByte(0xd000));
}

TEST_F(DmgMemoryTest, VramIsntBanked)
{
	// The boot ROM is running with the LCD off
	DmgMemory().WriteByte(0x8000, 0x12);
	DmgMemory().WriteByte(Vbk, 1);
	EXPECT_EQ(0x12, DmgMemory().ReadByte(0x8000));
}

TEST_F(DmgMemoryTest, UnusableAreaReadsZero)
{
	EXPECT_EQ(0x00, DmgMemory().ReadByte(0xfea0));
	EXPECT_EQ(0x00, DmgMemory().ReadByte(0xfeff));
}

TEST_F(DmgMemoryTest, SerialControlBit1AlwaysReadsOne)
{
	DmgMemory().WriteByte(0xff02, 0x00);
	EXPECT_EQ(0x7e, DmgMemory().ReadByte(0xff02));
}
