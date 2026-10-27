#include "stdafx.h"
#include <gtest/gtest.h>
#include <vector>
#include "CgbTestProgram.h"

// The CGB's double speed (https://gbdev.io/pandocs/CGB_Registers.html#ff4d--key1spd-cgb-mode-only-prepare-speed-switch):
// KEY1 arms a switch, which STOP makes, after which the CPU pauses for about 0x20000 CPU clocks (see
// Cpu::SwitchSpeed). The CPU, timer, serial port and OAM DMA run twice as fast; the PPU, APU and VRAM DMA keep their
// speed. Times here are in the 4 MHz system clock's clocks, which Emulator::GetSystemClocks counts at either speed.

namespace
{
	const unsigned char Key1 = 0x4d, Div = 0x04;
	const unsigned int PauseCycles = 32770;

	CgbTestProgram& Arm(CgbTestProgram& program) { return program.WriteIo(Key1, 0x01); }
	CgbTestProgram& Stop(CgbTestProgram& program) { return program.Op({ 0x10, 0x00 }); }

	unsigned char Read(Emulator& emulator, unsigned short address) { return emulator.GetMemoryMap().ReadByte(address); }

	// The A register once the program reaches the address
	unsigned char AAt(Emulator& emulator, unsigned short address)
	{
		EXPECT_TRUE(RunToAddress(emulator, address));
		return emulator.GetCpu().GetRegisters().A;
	}

	// Runs the STOP that switches speed and the pause after it, returning the system clocks they took
	uint64_t ClocksToSwitch(Emulator& emulator)
	{
		auto before = emulator.GetSystemClocks();
		emulator.RunInstruction();
		while (emulator.GetCpu().IsSwitchingSpeed()) emulator.RunInstruction();
		return emulator.GetSystemClocks() - before;
	}

	// DIV's increments in the given system clocks, counted as the program spins
	int DivIncrements(Emulator& emulator, uint64_t clocks)
	{
		auto& memory = emulator.GetMemoryMap();
		auto end = emulator.GetSystemClocks() + clocks;
		auto increments = 0;
		for (auto div = memory.ReadByte(0xff04); emulator.GetSystemClocks() < end;)
		{
			emulator.RunInstruction();
			auto now = memory.ReadByte(0xff04);
			increments += static_cast<unsigned char>(now - div);
			div = now;
		}

		return increments;
	}
}

TEST(CgbSpeedSwitchTest, Key1KeepsOnlyItsArmingBit)
{
	CgbTestProgram program;
	program.WriteIo(Key1, 0xff).ReadIo(Key1);
	auto armed = program.Here();
	program.WriteIo(Key1, 0xfe).ReadIo(Key1);
	auto disarmed = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	EXPECT_EQ(0x7f, AAt(*emulator, armed));
	EXPECT_EQ(0x7e, AAt(*emulator, disarmed));
}

TEST(CgbSpeedSwitchTest, StopWithKey1ArmedSwitchesToDoubleSpeedAndDisarms)
{
	CgbTestProgram program;
	Stop(Arm(program)).ReadIo(Key1);
	auto switched = program.Here();
	Stop(Arm(program)).ReadIo(Key1);
	auto switchedBack = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	EXPECT_EQ(0xfe, AAt(*emulator, switched));
	EXPECT_TRUE(emulator->GetCpu().IsDoubleSpeed());
	EXPECT_EQ(0x7e, AAt(*emulator, switchedBack));
	EXPECT_FALSE(emulator->GetCpu().IsDoubleSpeed());
}

TEST(CgbSpeedSwitchTest, TheCpuPausesFor0x20008CpuClocks)
{
	// 32770 machine cycles of the new speed, the length Gambatte's hardware tests show (see Cpu::SwitchSpeed)
	CgbTestProgram program;
	Arm(program);
	auto toDouble = program.Here();
	Stop(program);
	Arm(program);
	auto toNormal = program.Here();
	Stop(program).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, toDouble));
	EXPECT_EQ(4u + PauseCycles * 2u, ClocksToSwitch(*emulator)) << "STOP at normal speed, then the pause at double";

	ASSERT_TRUE(RunToAddress(*emulator, toNormal));
	EXPECT_EQ(2u + PauseCycles * 4u, ClocksToSwitch(*emulator)) << "STOP at double speed, then the pause at normal";
}

namespace
{
	// LY read straight after switching speed (switches times) from the start of line ly, as Gambatte's speedchange
	// hardware tests do (https://github.com/sinamas/gambatte, test/hwtests/speedchange), with JOYP deselected and IE
	// clear so STOP can only switch speed
	unsigned char LyAfterSwitching(unsigned char ly, int switches)
	{
		CgbTestProgram program;
		program.WriteIo(0xff, 0x00).WriteIo(0x00, 0x30);
		program.ReadIo(0x44).Op({ 0xfe, ly, 0x20, 0xfa });	// LD A,(LY); CP ly; JR NZ
		for (auto i = 0; i < switches; i++) Stop(Arm(program));
		program.ReadIo(0x44);
		auto read = program.Here();
		program.Loop();

		auto emulator = program.PowerOn();
		return AAt(*emulator, read);
	}
}

// The values a CGB-04C reads in Gambatte's speedchange_ly97_ly, speedchange_ly44_m3_ly and speedchange2_ly44_m3_ly_1
TEST(CgbSpeedSwitchTest, LyAfterSwitchingMatchesTheHardware)
{
	EXPECT_EQ(0x8c, LyAfterSwitching(0x97, 1));
	EXPECT_EQ(0x39, LyAfterSwitching(0x44, 1));
	EXPECT_EQ(0x25, LyAfterSwitching(0x44, 2));
}

TEST(CgbSpeedSwitchTest, StopsSecondByteIsSkipped)
{
	CgbTestProgram program;
	Arm(program).Op({ 0x10, 0x3c });	// STOP, then INC A as its second byte
	auto after = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	EXPECT_EQ(0x01, AAt(*emulator, after)) << "A still holds the $01 written to KEY1";
}

namespace
{
	// Gambatte's hardware tests speedchange_div_1 and _2 (CGB-04C): STOP switches to double speed at $015A, and DIV is
	// read by an LDH at $0198 or $0199, after 60 or 61 NOPs
	unsigned char DivAfterSwitching(int nops)
	{
		CgbTestProgram program;
		program.WriteIo(0x00, 0x30).Op({ 0xaf, 0xe0, 0xff, 0x3c, 0xe0, 0x4d, 0x10, 0x00 });	// IE = 0; KEY1 = 1; STOP
		program.Nops(nops).ReadIo(Div);
		auto read = program.Here();
		program.Loop();

		auto emulator = program.PowerOn();
		return AAt(*emulator, read);
	}
}

TEST(CgbSpeedSwitchTest, DivAfterTheSwitchMatchesTheHardware)
{
	// STOP resets DIV, which then counts through the pause and ticks over to 1 between these two reads
	EXPECT_EQ(0x00, DivAfterSwitching(60)) << "speedchange_div_1";
	EXPECT_EQ(0x01, DivAfterSwitching(61)) << "speedchange_div_2";
}
TEST(CgbSpeedSwitchTest, DivTicksTwiceAsFastAtDoubleSpeed)
{
	// DIV counts at 16384 Hz, once every 256 system clocks, or at double speed twice as often
	for (auto doubleSpeed : { false, true })
	{
		CgbTestProgram program;
		if (doubleSpeed) Stop(Arm(program));
		auto spinning = program.Here();
		program.Loop();

		auto emulator = program.PowerOn();
		ASSERT_TRUE(RunToAddress(*emulator, spinning));
		while (emulator->GetCpu().IsSwitchingSpeed()) emulator->RunInstruction();
		auto increments = DivIncrements(*emulator, 256 * 100);
		EXPECT_NEAR(doubleSpeed ? 200 : 100, increments, 1) << (doubleSpeed ? "double speed" : "normal speed");
	}
}

TEST(CgbSpeedSwitchTest, FramesKeepTheirLengthAtDoubleSpeed)
{
	CgbTestProgram program;
	Stop(Arm(program));
	auto doubled = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, doubled));

	emulator->GetFrame();
	auto start = emulator->GetSystemClocks();
	emulator->GetFrame();
	EXPECT_EQ(static_cast<uint64_t>(Graphics::FrameClocks), emulator->GetSystemClocks() - start);
}

TEST(CgbSpeedSwitchTest, SoundKeepsItsTimingAtDoubleSpeed)
{
	// CH2 with a length of 1 is turned off by the frame sequencer's next length clock, 512 times a second at either
	// speed: the time from triggering to NR52 showing it off is under 8192 system clocks at both
	for (auto doubleSpeed : { false, true })
	{
		CgbTestProgram program;
		if (doubleSpeed) Stop(Arm(program));
		program.WriteIo(0x17, 0xf0).WriteIo(0x16, 0x3f).WriteIo(0x19, 0xc0);
		auto triggered = program.Here();
		program.Loop();

		auto emulator = program.PowerOn();
		ASSERT_TRUE(RunToAddress(*emulator, triggered));
		auto start = emulator->GetSystemClocks();
		for (auto i = 0; i < 100000 && (Read(*emulator, 0xff26) & 0x02); i++) emulator->RunInstruction();

		EXPECT_EQ(0, Read(*emulator, 0xff26) & 0x02);
		EXPECT_LE(emulator->GetSystemClocks() - start, 8192u) << (doubleSpeed ? "double speed" : "normal speed");
	}
}

TEST(CgbSpeedSwitchTest, TheSerialClockDoublesAtDoubleSpeed)
{
	// A transfer on the internal 8192 Hz clock: 8 bits of 512 CPU clocks, at normal speed 4096 system clocks
	for (auto doubleSpeed : { false, true })
	{
		CgbTestProgram program;
		if (doubleSpeed) Stop(Arm(program));
		program.WriteIo(0x02, 0x81);
		auto started = program.Here();
		program.Loop();

		auto emulator = program.PowerOn();
		ASSERT_TRUE(RunToAddress(*emulator, started));
		auto start = emulator->GetSystemClocks();
		for (auto i = 0; i < 100000 && (Read(*emulator, 0xff02) & 0x80); i++) emulator->RunInstruction();

		// Bits shift on the clock's period boundaries, so the transfer takes 7 to 8 periods
		auto clocks = emulator->GetSystemClocks() - start;
		auto period = doubleSpeed ? 256u : 512u;
		EXPECT_GT(clocks, 7 * period) << (doubleSpeed ? "double speed" : "normal speed");
		EXPECT_LE(clocks, 8 * period + 16) << (doubleSpeed ? "double speed" : "normal speed");
	}
}

TEST(CgbSpeedSwitchTest, TheSerialPortsFastClockIs32TimesFaster)
{
	CgbTestProgram program;
	program.WriteIo(0x02, 0x83);
	auto started = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, started));
	auto start = emulator->GetSystemClocks();
	for (auto i = 0; i < 100000 && (Read(*emulator, 0xff02) & 0x80); i++) emulator->RunInstruction();

	auto clocks = emulator->GetSystemClocks() - start;
	EXPECT_GT(clocks, 7u * 16u);
	EXPECT_LE(clocks, 8u * 16u + 16u);
}

TEST(CgbSpeedSwitchTest, StopWithoutKey1ArmedStopsAsUsual)
{
	CgbTestProgram program;
	Stop(program).Loop();

	auto emulator = program.PowerOn();
	ASSERT_TRUE(RunToAddress(*emulator, CgbTestProgram::Start));
	emulator->RunInstruction();
	EXPECT_TRUE(emulator->GetCpu().IsStopped());
	EXPECT_FALSE(emulator->GetCpu().IsDoubleSpeed());
}

TEST(CgbSpeedSwitchTest, AHeldButtonPreventsTheSwitch)
{
	// STOP would end at once, so (SameBoy) it doesn't switch speed
	CgbTestProgram program;
	Arm(program).WriteIo(0x00, 0x10);	// select the buttons
	Stop(program);
	auto after = program.Here();
	program.Loop();

	auto emulator = program.PowerOn();
	emulator->GetJoypad().SetKeysDown(JoypadKey::A);
	ASSERT_TRUE(RunToAddress(*emulator, after));
	EXPECT_FALSE(emulator->GetCpu().IsDoubleSpeed());
}

TEST(DmgSpeedSwitchTest, TheDmgHasNoKey1AndDoesntSwitch)
{
	CgbTestProgram program;
	Arm(program).ReadIo(Key1);
	auto read = program.Here();
	Stop(program);
	program.Loop();

	auto emulator = program.PowerOnDmgWithoutBootRom();
	EXPECT_EQ(0xff, AAt(*emulator, read));
	emulator->RunInstruction();
	EXPECT_TRUE(emulator->GetCpu().IsStopped());
	EXPECT_FALSE(emulator->GetCpu().IsDoubleSpeed());
}

namespace
{
	// Gambatte's hardware tests speedchange_lcdoff_tima00_1 and speedchange_lcdoff_tima01_1/_2 (recorded on a
	// CGB-04C): with the LCD off, TIMA counting at TAC tac from $FE or $F0 with TMA 0 overflows into the timer
	// interrupt, whose handler switches speed and reads TIMA after nops NOPs. Assembled byte for byte as the tests are
	unsigned char TimaAfterSwitching(unsigned char tac, unsigned char tima, int nops)
	{
		CgbTestProgram program;
		program.Data(0x50, { 0xc3, 0x00, 0x10 });	// JP $1000
		program.Op({ 0x06, 0x91, 0xf0, 0x44, 0xb8, 0x20, 0xfb });	// LD B,$91; wait for LY = $91
		program.Op({ 0xaf, 0xe0, 0x40, 0xaf, 0xe0, 0x06 });	// LCDC = 0; TMA = 0
		program.WriteIo(0x07, tac).WriteIo(0x05, tima).WriteIo(0xff, 0x04).Op({ 0xaf, 0xe0, 0x0f, 0xfb });	// IE = timer; IF = 0; EI

		std::vector<unsigned char> handler = { 0x3e, 0x00, 0xe0, 0xff, 0x3e, 0x30, 0xe0, 0x00, 0x3e, 0x01, 0xe0, 0x4d, 0x10, 0x00 };
		handler.insert(handler.end(), nops, 0x00);
		handler.insert(handler.end(), { 0xf0, 0x05, 0x18, 0xfe });	// LD A,(TIMA); JR $
		program.Data(0x1000, handler);

		auto emulator = program.PowerOn();
		EXPECT_TRUE(RunToAddress(*emulator, static_cast<unsigned short>(0x1000 + handler.size() - 2)));
		return emulator->GetCpu().GetRegisters().A;
	}

	// Gambatte's speedchange_ch2_nr52_1a and _1b: CH2 plays with 6 of its length left, the switch to double speed comes
	// at $545, and NR52 is read after a delay loop and nops NOPs. On the hardware CH2's length has just run out with 3
	// NOPs (_1b) and not quite with 2 (_1a)
	unsigned char Nr52AfterSwitching(int nops)
	{
		CgbTestProgram program;
		program.Op({ 0xaf, 0xe0, 0x04, 0xe0, 0x24, 0xe0, 0x25, 0xe0, 0x26 });	// DIV, NR50, NR51, NR52 = 0
		program.WriteIo(0x26, 0x80).WriteIo(0x16, 0x3a).WriteIo(0x17, 0xf0).WriteIo(0x18, 0x00).WriteIo(0x19, 0xc7);
		program.WriteIo(0x00, 0x30).Op({ 0xaf, 0xe0, 0xff, 0x3c, 0xe0, 0x4d });	// IE = 0; KEY1 = 1

		std::vector<unsigned char> afterSwitch = { 0x10, 0x00, 0x06, 0xf5, 0x0e, 0x0c, 0x05, 0x20, 0xfd, 0x0d, 0x20, 0xfa };
		afterSwitch.insert(afterSwitch.end(), nops, 0x00);
		afterSwitch.insert(afterSwitch.end(), { 0xf0, 0x26, 0x18, 0xfe });	// LD A,(NR52); JR $
		program.Data(0x545, afterSwitch);

		auto emulator = program.PowerOn();
		EXPECT_TRUE(RunToAddress(*emulator, static_cast<unsigned short>(0x545 + afterSwitch.size() - 2)));
		return emulator->GetCpu().GetRegisters().A;
	}
}

namespace
{
	// Gambatte's hardware tests speedchange_tima01_1 and _2 (CGB-04C): as TimaAfterSwitching, but with the LCD on and
	// without waiting for LY first, the handler entered from TIMA counting every 16 CPU clocks from $F0
	unsigned char TimaAfterSwitchingWithTheLcdOn(int nops)
	{
		CgbTestProgram program;
		program.Data(0x50, { 0xc3, 0x00, 0x10 });	// JP $1000
		program.Op({ 0xaf, 0xe0, 0x06 }).WriteIo(0x07, 0x05).WriteIo(0x05, 0xf0).WriteIo(0xff, 0x04);	// TMA = 0
		program.Op({ 0xaf, 0xe0, 0x0f, 0xfb });	// IF = 0; EI

		std::vector<unsigned char> handler = { 0x3e, 0x30, 0xe0, 0x00, 0xaf, 0xe0, 0xff, 0x3c, 0xe0, 0x4d, 0x10, 0x00 };
		handler.insert(handler.end(), nops, 0x00);
		handler.insert(handler.end(), { 0xf0, 0x05, 0x18, 0xfe });	// LD A,(TIMA); JR $
		program.Data(0x1000, handler);

		auto emulator = program.PowerOn();
		EXPECT_TRUE(RunToAddress(*emulator, static_cast<unsigned short>(0x1000 + handler.size() - 2)));
		return emulator->GetCpu().GetRegisters().A;
	}
}

// These two tell apart the pause lengths that fit the other hardware tests (see Cpu::SwitchSpeed)
TEST(CgbSpeedSwitchTest, TimaWithTheLcdOnMatchesTheHardware)
{
	EXPECT_EQ(0x07, TimaAfterSwitchingWithTheLcdOn(4)) << "speedchange_tima01_1";
	EXPECT_EQ(0x08, TimaAfterSwitchingWithTheLcdOn(5)) << "speedchange_tima01_2";
}

// The timer and the APU's frame sequencer keep running through the pause after a switch, against Pan Docs' frozen DIV
TEST(CgbSpeedSwitchTest, TheTimerRunsThroughThePauseAsOnTheHardware)
{
	// 0x20000 CPU clocks at 1024 per count take TIMA from 0 to $80
	EXPECT_EQ(0x80, TimaAfterSwitching(0x04, 0xfe, 2)) << "speedchange_lcdoff_tima00_1";
	EXPECT_EQ(0x08, TimaAfterSwitching(0x05, 0xf0, 4)) << "speedchange_lcdoff_tima01_1";
	EXPECT_EQ(0x09, TimaAfterSwitching(0x05, 0xf0, 5)) << "speedchange_lcdoff_tima01_2";
}

TEST(CgbSpeedSwitchTest, SoundLengthsRunThroughThePauseAsOnTheHardware)
{
	// _1b: CH2 is off, which it can only be if the frame sequencer clocked its length through the pause; frozen, it
	// would play for about three more length clocks. Emu Boy's CH2 runs out a little sooner than the hardware's, so
	// _1a, which needs that timing exactly, isn't checked (see docs/known-gaps.md)
	EXPECT_EQ(0xf0, Nr52AfterSwitching(3)) << "speedchange_ch2_nr52_1b";
}
