#include "stdafx.h"
#include <gtest/gtest.h>
#include <array>
#include "GraphicsTestFixture.h"

// The DMG's OAM corruption bug, per https://gbdev.io/pandocs/OAM_Corruption_Bug.html: while the PPU scans OAM in
// mode 2, reading a row of four 16-bit words each machine cycle, a CPU access to $FE00-$FEFF, or a 16-bit increment
// or decrement of a register holding such an address, corrupts the row being read. Blargg's oam_bug ROMs check the
// timing and the patterns on hardware.

namespace
{
	using Oam = std::array<unsigned char, Graphics::OamSize>;

	const int RowBytes = 8;

	unsigned short Word(const Oam& oam, int row, int index) { return static_cast<unsigned short>(oam[row * RowBytes + index * 2] | oam[row * RowBytes + index * 2 + 1] << 8); }

	void SetWord(Oam& oam, int row, int index, unsigned short value)
	{
		oam[row * RowBytes + index * 2] = value & 0xff;
		oam[row * RowBytes + index * 2 + 1] = value >> 8;
	}

	void CopyRow(Oam& oam, int from, int to)
	{
		for (auto i = 0; i < RowBytes; i++) oam[to * RowBytes + i] = oam[from * RowBytes + i];
	}

	// The patterns as Pan Docs gives them: the first word of the row is replaced by a bitwise function of it (a),
	// the preceding row's first word (b) and the preceding row's third word (c), and the other three words are
	// copied from the preceding row
	void WriteCorruption(Oam& oam, int row)
	{
		unsigned short a = Word(oam, row, 0), b = Word(oam, row - 1, 0), c = Word(oam, row - 1, 2);
		SetWord(oam, row, 0, static_cast<unsigned short>(((a ^ c) & (b ^ c)) ^ c));
		for (auto i = 1; i < 4; i++) SetWord(oam, row, i, Word(oam, row - 1, i));
	}

	void ReadCorruption(Oam& oam, int row)
	{
		unsigned short a = Word(oam, row, 0), b = Word(oam, row - 1, 0), c = Word(oam, row - 1, 2);
		SetWord(oam, row, 0, static_cast<unsigned short>(b | (a & c)));
		for (auto i = 1; i < 4; i++) SetWord(oam, row, i, Word(oam, row - 1, i));
	}

	// Except in the first four rows and the last, the preceding row's first word is corrupted and the row is
	// copied over the current row and the row two before, then a read corruption follows
	void ReadDuringIncDecCorruption(Oam& oam, int row)
	{
		if (row >= 4 && row < 19)
		{
			unsigned short a = Word(oam, row - 2, 0), b = Word(oam, row - 1, 0), c = Word(oam, row, 0), d = Word(oam, row - 1, 2);
			SetWord(oam, row - 1, 0, static_cast<unsigned short>((b & (a | c | d)) | (a & c & d)));
			CopyRow(oam, row - 1, row);
			CopyRow(oam, row - 1, row - 2);
		}

		ReadCorruption(oam, row);
	}

	// Machine cycles in which the PPU reads row r of OAM start at position 4r in a line
	int RowPosition(int row) { return 4 * row; }
}

class OamCorruptionTests : public GraphicsTestFixture
{
public:
	// Every byte different, as in blargg's tests
	OamCorruptionTests()
	{
		for (unsigned short i = 0; i < Graphics::OamSize; i++) Ppu.WriteOamDirect(i, static_cast<unsigned char>(i + 0x0c));
	}

	Oam CurrentOam() const
	{
		Oam oam;
		for (unsigned short i = 0; i < Graphics::OamSize; i++) oam[i] = Ppu.OamByte(i);
		return oam;
	}
};

TEST_F(OamCorruptionTests, WriteCorruptsRowBeingScanned)
{
	auto expected = CurrentOam();
	WriteCorruption(expected, 6);

	TurnOn();
	RunTo(1, RowPosition(6));
	Ppu.CorruptOam(OamCorruption::Write);

	EXPECT_EQ(expected, CurrentOam());
}

TEST_F(OamCorruptionTests, ReadCorruptsRowBeingScanned)
{
	auto expected = CurrentOam();
	ReadCorruption(expected, 9);

	TurnOn();
	RunTo(1, RowPosition(9));
	Ppu.CorruptOam(OamCorruption::Read);

	EXPECT_EQ(expected, CurrentOam());
}

TEST_F(OamCorruptionTests, ReadDuringIncDecCorruptsThreeRows)
{
	auto expected = CurrentOam();
	ReadDuringIncDecCorruption(expected, 6);

	TurnOn();
	RunTo(1, RowPosition(6));
	Ppu.CorruptOam(OamCorruption::ReadDuringIncDec);

	EXPECT_EQ(expected, CurrentOam());
}

TEST_F(OamCorruptionTests, ReadDuringIncDecIsAPlainReadInFirstFourRowsAndLastRow)
{
	for (auto row : { 1, 3, 19 })
	{
		auto expected = CurrentOam();
		ReadCorruption(expected, row);

		TurnOn();
		RunTo(1, RowPosition(row));
		Ppu.CorruptOam(OamCorruption::ReadDuringIncDec);

		EXPECT_EQ(expected, CurrentOam()) << "row " << row;
	}
}

TEST_F(OamCorruptionTests, EachRowIsCorruptedInItsMachineCycleOfTheScan)
{
	TurnOn();

	for (auto row = 1; row < 20; row++)
	{
		auto expected = CurrentOam();
		WriteCorruption(expected, row);

		RunTo(1 + row, RowPosition(row));
		Ppu.CorruptOam(OamCorruption::Write);

		EXPECT_EQ(expected, CurrentOam()) << "row " << row;
	}
}

TEST_F(OamCorruptionTests, NothingIsCorruptedOutsideTheScanOrInTheFirstRow)
{
	auto original = CurrentOam();

	// LCD off
	Ppu.CorruptOam(OamCorruption::Write);
	EXPECT_EQ(original, CurrentOam());

	// The first line after the LCD is turned on has no OAM scan
	TurnOn();
	Ppu.CorruptOam(OamCorruption::Write);
	EXPECT_EQ(original, CurrentOam());

	// The first row, then just after the scan, mode 3, H-Blank and V-Blank
	for (auto position : { 0, 80, 84, 300 })
	{
		RunTo(1, position);
		Ppu.CorruptOam(OamCorruption::Write);
		EXPECT_EQ(original, CurrentOam()) << "at " << position;
	}

	RunTo(Graphics::VertPixels, 20);
	Ppu.CorruptOam(OamCorruption::Write);
	EXPECT_EQ(original, CurrentOam());
}

// The CPU instructions that trigger the bug, and in which of their machine cycles
class CpuOamCorruptionTests : public OamCorruptionTests
{
	class PpuClock : public ClockedHardware
	{
		Graphics& _graphics;

	public:
		explicit PpuClock(Graphics& graphics) : _graphics(graphics) {}
		void RunCycles(int cycles) override { _graphics.RunCycles(cycles); }
	};

	PpuClock _clock{ Ppu };

public:
	CpuOamCorruptionTests()
	{
		Processor.SetClockedHardware(&_clock);
		Processor.GetRegisters().PC = 0;
		TurnOn();
	}

	// Runs the instruction at $0000 so that its given machine cycle (counting the opcode fetch as 1) is the one in
	// which the PPU scans the row
	void RunInstruction(std::vector<unsigned char>&& instruction, int cycle, int row)
	{
		Memory.SetBytes(MemoryMap::RomFixed, std::move(instruction));
		RunTo(1, RowPosition(row) - 4 * cycle);
		Processor.DoNextInstruction();
	}
};

TEST_F(CpuOamCorruptionTests, IncrementOfRegisterPairHoldingOamAddressIsAWrite)
{
	// INC DE: the increment is in its second machine cycle
	auto expected = CurrentOam();
	WriteCorruption(expected, 7);

	Processor.GetRegisters().DE = 0xfe00;
	RunInstruction({ 0x13 }, 2, 7);

	EXPECT_EQ(expected, CurrentOam());
	EXPECT_EQ(0xfe01, Processor.GetRegisters().DE);
}

TEST_F(CpuOamCorruptionTests, DecrementOfAnyAddressFromFE00ToFEFFIsAWrite)
{
	// DEC SP
	auto expected = CurrentOam();
	WriteCorruption(expected, 7);

	Processor.GetRegisters().SP = 0xfeff;
	RunInstruction({ 0x3b }, 2, 7);

	EXPECT_EQ(expected, CurrentOam());
}

TEST_F(CpuOamCorruptionTests, IncrementOutsideOamDoesNothing)
{
	auto original = CurrentOam();

	Processor.GetRegisters().DE = 0xfd00;
	RunInstruction({ 0x13 }, 2, 7);

	EXPECT_EQ(original, CurrentOam());
}

TEST_F(CpuOamCorruptionTests, SixteenBitAddDoesNothing)
{
	// Blargg's 3-non_causes: ADD HL,DE
	auto original = CurrentOam();

	Processor.GetRegisters().HL = 0xfe00;
	Processor.GetRegisters().DE = 0x0001;
	RunInstruction({ 0x19 }, 2, 7);

	EXPECT_EQ(original, CurrentOam());
}

TEST_F(CpuOamCorruptionTests, LoadWithIncrementIsAReadDuringIncDec)
{
	// LD A,(HL+): the read and the increment share its second machine cycle
	auto expected = CurrentOam();
	ReadDuringIncDecCorruption(expected, 8);

	Processor.GetRegisters().HL = 0xfe40;
	RunInstruction({ 0x2a }, 2, 8);

	EXPECT_EQ(expected, CurrentOam());
	EXPECT_EQ(0xff, Processor.GetRegisters().A);
}

TEST_F(CpuOamCorruptionTests, PopIsAReadDuringIncDecThenARead)
{
	// Blargg's 8-instr_effect: POP's first read shares its machine cycle with SP's increment
	auto expected = CurrentOam();
	ReadDuringIncDecCorruption(expected, 8);
	ReadCorruption(expected, 9);

	Processor.GetRegisters().SP = 0xfef0;
	RunInstruction({ 0xc1 }, 2, 8);

	EXPECT_EQ(expected, CurrentOam());
}

TEST_F(CpuOamCorruptionTests, PushIsThreeWrites)
{
	// The decrement before the first write, then the two writes
	auto expected = CurrentOam();
	WriteCorruption(expected, 8);
	WriteCorruption(expected, 9);
	WriteCorruption(expected, 10);

	Processor.GetRegisters().SP = 0xfef0;
	RunInstruction({ 0xc5 }, 2, 8);

	EXPECT_EQ(expected, CurrentOam());
}
