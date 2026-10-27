#include "stdafx.h"
#include <gtest/gtest.h>
#include <vector>
#include "CgbTestProgram.h"

// The CGB's VRAM DMA (https://gbdev.io/pandocs/CGB_Registers.html#lcd-vram-dma-transfers), with SameBoy's timing
// (GB_hdma_run): the CPU waits from the opcode fetch after the transfer starts, for a machine cycle to set up and then
// 2 bytes per microsecond

namespace
{
	const unsigned char Hdma1 = 0x51, Hdma2 = 0x52, Hdma3 = 0x53, Hdma4 = 0x54, Hdma5 = 0x55, Lcdc = 0x40, Vbk = 0x4f;
	const unsigned short SourceData = 0x4000;
	const unsigned char HBlankTransfer = 0x80;

	std::vector<unsigned char> Pattern(size_t size)
	{
		std::vector<unsigned char> bytes(size);
		for (size_t i = 0; i < size; i++) bytes[i] = static_cast<unsigned char>(i * 7 + 1);
		return bytes;
	}

	// Sets the source and destination registers
	CgbTestProgram& Addresses(CgbTestProgram& program, unsigned short source, unsigned short destination)
	{
		return program.WriteIo(Hdma1, source >> 8).WriteIo(Hdma2, source & 0xff)
			.WriteIo(Hdma3, destination >> 8).WriteIo(Hdma4, destination & 0xff);
	}

	unsigned char Read(Emulator& emulator, unsigned short address) { return emulator.GetMemoryMap().ReadByte(address); }

	// VRAM as the CPU sees it with the LCD off, in the bank VBK selects
	std::vector<unsigned char> ReadVram(Emulator& emulator, unsigned short address, size_t size)
	{
		emulator.GetMemoryMap().WriteByte(0xff40, 0x00);
		std::vector<unsigned char> bytes;
		for (size_t i = 0; i < size; i++) bytes.push_back(Read(emulator, static_cast<unsigned short>(address + i)));
		return bytes;
	}

	uint64_t ClocksForInstruction(Emulator& emulator)
	{
		auto before = emulator.GetSystemClocks();
		emulator.RunInstruction();
		return emulator.GetSystemClocks() - before;
	}

	// Switches to double speed: KEY1 = 1; STOP
	CgbTestProgram& SwitchToDoubleSpeed(CgbTestProgram& program) { return program.WriteIo(0x4d, 0x01).Op({ 0x10, 0x00 }); }

	// Runs instructions until LY reaches the line
	void RunToLine(Emulator& emulator, int line)
	{
		for (auto i = 0; i < 100000 && Read(emulator, 0xff44) != line; i++) emulator.RunInstruction();
		ASSERT_EQ(line, Read(emulator, 0xff44));
	}
}

TEST(VramDmaTest, GeneralPurposeTransferCopiesEveryBlockAtOnce)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x40));
	program.WriteIo(Lcdc, 0x00);
	Addresses(program, SourceData, 0x8100).WriteIo(Hdma5, 0x03);
	auto end = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));
	emulator->RunInstruction();

	EXPECT_EQ(Pattern(0x40), ReadVram(*emulator, 0x8100, 0x40));
	EXPECT_EQ(0xff, Read(*emulator, 0xff55)) << "HDMA5 reads $FF once the transfer completes";
}

TEST(VramDmaTest, GeneralPurposeTransferHoldsTheCpuFor9MachineCyclesPerBlock)
{
	for (unsigned char blocks = 1; blocks <= 4; blocks++)
	{
		CgbTestProgram program;
		program.Data(SourceData, Pattern(0x40));
		program.WriteIo(Lcdc, 0x00);
		Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, static_cast<unsigned char>(blocks - 1));
		auto nop = program.Here();
		program.Nops(2).Loop();

		auto emulator = program.PowerOn();
		ASSERT_TRUE(RunToAddress(*emulator, nop));

		// The NOP's own machine cycle, one to set up, then 8 per 16 bytes
		EXPECT_EQ(4u + 4u + blocks * 8u * 4u, ClocksForInstruction(*emulator)) << static_cast<int>(blocks) << " blocks";
		EXPECT_EQ(4u, ClocksForInstruction(*emulator)) << "the next instruction runs as normal";
	}
}

TEST(VramDmaTest, AtDoubleSpeedTheTransferTakesAsLongInSystemClocks)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x10));
	program.WriteIo(Lcdc, 0x00);
	SwitchToDoubleSpeed(program);
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, 0x00);
	auto nop = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, nop));
	ASSERT_TRUE(emulator->GetCpu().IsDoubleSpeed());

	// At double speed a machine cycle is 2 system clocks, and each copies 1 byte: the NOP, 1 to set up, 16 to copy
	EXPECT_EQ((1u + 1u + 16u) * 2u, ClocksForInstruction(*emulator));
	EXPECT_EQ(Pattern(0x10), ReadVram(*emulator, 0x8000, 0x10));
}

TEST(VramDmaTest, LowFourBitsAndTheDestinationsTopBitsAreIgnored)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x10));
	program.WriteIo(Lcdc, 0x00);
	Addresses(program, SourceData | 0x0f, 0xe20f).WriteIo(Hdma5, 0x00);
	auto end = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));
	emulator->RunInstruction();

	EXPECT_EQ(Pattern(0x10), ReadVram(*emulator, 0x8200, 0x10));
}

TEST(VramDmaTest, CopiesToTheSelectedVramBank)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x10));
	program.WriteIo(Lcdc, 0x00).WriteIo(Vbk, 1);
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, 0x00);
	auto end = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));
	emulator->RunInstruction();

	EXPECT_EQ(Pattern(0x10), ReadVram(*emulator, 0x8000, 0x10));
	emulator->GetMemoryMap().WriteByte(0xff4f, 0);
	EXPECT_EQ(std::vector<unsigned char>(0x10), ReadVram(*emulator, 0x8000, 0x10));
}

TEST(VramDmaTest, SourcesOutsideRomAndRamCopyFf)
{
	// Work RAM's echo at $E000 and VRAM aren't read; work RAM at $C000 is
	CgbTestProgram program;
	program.WriteIo(Lcdc, 0x00);
	program.Op({ 0x3e, 0x5a, 0xea, 0x00, 0xc0 });	// LD A,$5A; LD ($C000),A
	Addresses(program, 0xe000, 0x8000).WriteIo(Hdma5, 0x00);
	Addresses(program, 0x9000, 0x8010).WriteIo(Hdma5, 0x00);
	Addresses(program, 0xc000, 0x8020).WriteIo(Hdma5, 0x00);
	auto end = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));
	emulator->RunInstruction();

	auto vram = ReadVram(*emulator, 0x8000, 0x21);
	EXPECT_EQ(0xff, vram[0x00]);
	EXPECT_EQ(0xff, vram[0x10]);
	EXPECT_EQ(0x5a, vram[0x20]);
}

TEST(VramDmaTest, TheDestinationWrapsFromTheEndOfVramToItsStart)
{
	// Only bits 0-12 address VRAM, so from $9FF0 the second block goes to $8000 (SameBoy and Gambatte)
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x20));
	program.WriteIo(Lcdc, 0x00);
	Addresses(program, SourceData, 0x9ff0).WriteIo(Hdma5, 0x01);
	auto end = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));
	EXPECT_EQ(4u + 4u + 16u * 4u, ClocksForInstruction(*emulator)) << "Both blocks are copied";

	auto pattern = Pattern(0x20);
	EXPECT_EQ(0xff, Read(*emulator, 0xff55));
	EXPECT_EQ(std::vector<unsigned char>(pattern.begin(), pattern.begin() + 0x10), ReadVram(*emulator, 0x9ff0, 0x10));
	EXPECT_EQ(std::vector<unsigned char>(pattern.begin() + 0x10, pattern.end()), ReadVram(*emulator, 0x8000, 0x10));
}

namespace
{
	// Gambatte's hardware tests dma_dst_wrap_1 and dma_dst_wrap_2 (recorded on a CGB-04C): a 2-block transfer from
	// $BFF0 to $xFF0, then ($8000) & 7. The second block comes from work RAM at $C000, which holds 1, and $8000 holds 0
	// before. With HDMA3 $DF the destination wraps to $8000 within the 16-bit address and reads 1; with $FF it passes
	// $FFFF, which stops the transfer after the first block, and reads 0
	unsigned char DestinationWrapResult(unsigned char hdma3)
	{
		CgbTestProgram program;
		program.WriteIo(Lcdc, 0x00);
		program.Op({ 0x3e, 0x00, 0xea, 0x00, 0x80 });	// LD A,0; LD ($8000),A
		program.Op({ 0x3e, 0x01, 0xea, 0x00, 0xc0 });	// LD A,1; LD ($C000),A
		program.WriteIo(Hdma1, 0xbf).WriteIo(Hdma2, 0xf0).WriteIo(Hdma3, hdma3).WriteIo(Hdma4, 0xff).WriteIo(Hdma5, 0x01);
		program.Op({ 0xfa, 0x00, 0x80, 0xe6, 0x07 });	// LD A,($8000); AND 7
		auto end = program.Here();
		program.Loop();

		auto emulator = program.PowerOn();
		EXPECT_TRUE(RunToAddress(*emulator, end));
		return emulator->GetCpu().GetRegisters().A;
	}
}

TEST(VramDmaTest, TheTransferStopsWhenTheWholeDestinationPassesFfff)
{
	EXPECT_EQ(1, DestinationWrapResult(0xdf)) << "dma_dst_wrap_1";
	EXPECT_EQ(0, DestinationWrapResult(0xff)) << "dma_dst_wrap_2";
}

TEST(VramDmaTest, HBlankTransferCopiesABlockEachHBlank)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x30));
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x02);
	auto started = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, started));

	// Started on line 0 before it's drawn: nothing is copied until its H-Blank. HDMA5's bit 7 reads 0 while it runs
	EXPECT_EQ(0x02, Read(*emulator, 0xff55));
	RunToLine(*emulator, 1);
	EXPECT_EQ(0x01, Read(*emulator, 0xff55));
	RunToLine(*emulator, 2);
	EXPECT_EQ(0x00, Read(*emulator, 0xff55));
	RunToLine(*emulator, 3);
	EXPECT_EQ(0xff, Read(*emulator, 0xff55));

	EXPECT_EQ(Pattern(0x30), ReadVram(*emulator, 0x8000, 0x30));
}

TEST(VramDmaTest, HBlankTransferWaitsThroughVBlank)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x20));
	program.ReadIo(0x44).Op({ 0xfe, 0x90, 0x20, 0xfa });	// LD A,(LY); CP 144; JR NZ
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x01);
	auto started = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, started));

	RunToLine(*emulator, 152);
	EXPECT_EQ(0x01, Read(*emulator, 0xff55)) << "No block in V-Blank";
	RunToLine(*emulator, 1);
	EXPECT_EQ(0x00, Read(*emulator, 0xff55)) << "Line 0's H-Blank copies the first";
}

TEST(VramDmaTest, StartedInHBlankTheFirstBlockIsCopiedAtOnce)
{
	// Mode 0 on line 0, just after the LCD is turned on, comes before the line is drawn, so line 1's H-Blank is used
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x20));
	program.ReadIo(0x44).Op({ 0xfe, 0x01, 0x20, 0xfa });	// LD A,(LY); CP 1; JR NZ
	program.ReadIo(0x41).Op({ 0xe6, 0x03, 0x20, 0xfa });	// LD A,(STAT); AND 3; JR NZ
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x01);
	auto started = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, started));
	auto line = Read(*emulator, 0xff44);
	emulator->RunInstruction();

	EXPECT_EQ(line, Read(*emulator, 0xff44));
	EXPECT_EQ(0x00, Read(*emulator, 0xff55));
}

TEST(VramDmaTest, StartedWithTheLcdOffTheFirstBlockIsCopiedAtOnce)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x20));
	program.WriteIo(Lcdc, 0x00);
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x01);
	auto started = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, started));
	EXPECT_EQ(4u + 4u + 8u * 4u, ClocksForInstruction(*emulator));

	// With no more H-Blanks the second block waits
	for (auto i = 0; i < 1000; i++) emulator->RunInstruction();
	EXPECT_EQ(0x00, Read(*emulator, 0xff55));
	EXPECT_EQ(Pattern(0x10), ReadVram(*emulator, 0x8000, 0x10));
}

TEST(VramDmaTest, CancellingAnHBlankTransferKeepsTheBlocksLeftWithBit7Set)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x40));
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x03);
	program.ReadIo(0x44).Op({ 0xfe, 0x01, 0x20, 0xfa });	// LD A,(LY); CP 1; JR NZ
	program.WriteIo(Hdma5, 0x00);
	auto cancelled = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, cancelled));

	// Line 0's block was copied, leaving 3: Pan Docs has HDMA5 read them less 1, with bit 7 set as it isn't running
	EXPECT_EQ(0x82, Read(*emulator, 0xff55));
	RunToLine(*emulator, 5);
	EXPECT_EQ(0x82, Read(*emulator, 0xff55));
	EXPECT_EQ(std::vector<unsigned char>(0x10), ReadVram(*emulator, 0x8010, 0x10));
}

TEST(VramDmaTest, HaltHoldsHBlankTransfersBack)
{
	// Halts until V-Blank with the transfer running: the H-Blanks it sleeps through copy nothing (Pan Docs)
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x40));
	program.WriteIo(0xff, 0x01).WriteIo(0x0f, 0x00);	// IE = V-Blank; IF = 0
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x03);
	program.Op({ 0x76, 0x00 });	// HALT; NOP
	auto woken = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, woken));
	EXPECT_EQ(144, Read(*emulator, 0xff44));
	EXPECT_EQ(0x03, Read(*emulator, 0xff55));

	// Once awake, the next frame's H-Blanks copy as normal
	RunToLine(*emulator, 2);
	EXPECT_EQ(0x01, Read(*emulator, 0xff55));
}

TEST(VramDmaTest, AWriteStartsANewTransferWhenNoneIsRunning)
{
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x20));
	program.WriteIo(Lcdc, 0x00);
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, 0x00);
	Addresses(program, SourceData + 0x10, 0x8000).WriteIo(Hdma5, 0x00);
	auto end = program.Here();
	program.Nops(1).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, end));
	emulator->RunInstruction();

	auto pattern = Pattern(0x20);
	EXPECT_EQ(std::vector<unsigned char>(pattern.begin() + 0x10, pattern.end()), ReadVram(*emulator, 0x8000, 0x10));
}

TEST(VramDmaTest, WakingFromHaltInAnHBlankCopiesTheBlockHeldBack)
{
	// Halted in line 1's mode 3, the CPU sleeps through the start of its H-Blank, then wakes there on the STAT
	// interrupt for mode 0 (with IME clear, so no handler runs). The block held back is copied as it wakes (SameBoy's
	// allow_hdma_on_wake; Pan Docs: the transfer resumes when the CPU does)
	CgbTestProgram program;
	program.Data(SourceData, Pattern(0x30));
	Addresses(program, SourceData, 0x8000).WriteIo(Hdma5, HBlankTransfer | 0x02);
	program.WriteIo(0x41, 0x08).WriteIo(0xff, 0x02);	// STAT: mode 0 interrupt; IE: STAT
	program.ReadIo(0x44).Op({ 0xfe, 0x01, 0x20, 0xfa });	// LD A,(LY); CP 1; JR NZ
	program.ReadIo(0x41).Op({ 0xe6, 0x03, 0xfe, 0x03, 0x20, 0xf8 });	// LD A,(STAT); AND 3; CP 3; JR NZ
	program.WriteIo(0x0f, 0x00).Op({ 0x76, 0x00 });	// IF = 0; HALT; NOP
	auto woken = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, woken));

	EXPECT_EQ(1, Read(*emulator, 0xff44));
	EXPECT_EQ(0, Read(*emulator, 0xff41) & 3);
	EXPECT_EQ(0x00, Read(*emulator, 0xff55)) << "Line 0's block and the one held back are copied, leaving 1";
	EXPECT_EQ(Pattern(0x20), ReadVram(*emulator, 0x8000, 0x20));
}
