#include "stdafx.h"
#include <gtest/gtest.h>
#include <algorithm>
#include "CpuTestFixture.h"
#include "../core/Emulator.h"
#include "TimerTestHardware.h"
#include "ApuTestHelpers.h"

// STOP on the DMG, per the "Using the STOP Instruction" diagram in
// https://gbdev.io/pandocs/Reducing_Power_Consumption.html (the DMG has no speed switch, so KEY1 never applies):
//
// - A button held and selected in JOYP, no interrupt pending: STOP is a 2-byte opcode, HALT mode is entered, DIV
//   isn't reset
// - A button held and selected, an interrupt pending (IE & IF != 0): STOP is a 1-byte opcode, the mode doesn't
//   change, DIV isn't reset
// - No such button, no interrupt pending: STOP is a 2-byte opcode, STOP mode is entered, DIV is reset
// - No such button, an interrupt pending: STOP is a 1-byte opcode, STOP mode is entered, DIV is reset
//
// STOP mode ends when one of P10-P13 goes low, that is when a button that JOYP selects is pressed. The system clock
// is stopped meanwhile, so the timer and the rest of the hardware it drives don't run

namespace
{
	const unsigned short JoypAddress = MemoryMap::JoypadPort;
	const unsigned short TimaAddress = 0xff05;
	const unsigned short TacAddress = 0xff07;
	const unsigned short LyAddress = 0xff44;
	const unsigned short StatAddress = 0xff41;

	// JOYP values that select the direction keys, the action buttons, or neither
	const unsigned char SelectDirections = 0x20;
	const unsigned char SelectButtons = 0x10;
	const unsigned char SelectNothing = 0x30;

	// STOP, then INC B, then INC A. A 2-byte STOP skips INC B
	const unsigned char IncB = 0x04;
	const unsigned char IncA = 0x3c;
}

class CpuStopTests : public CpuTestFixture
{
public:
	::Timer Timer;
	TimerTestHardware Hardware{ Timer };

	CpuStopTests()
	{
		Timer.SetCpu(&Cpu);
		MemoryMap.SetTimer(&Timer);
		Cpu.SetClockedHardware(&Hardware);
		Joypad.SetCpu(&Cpu);

		Cpu.Registers().PC = 0;
		Cpu.Registers().SP = StackTop;
		Cpu.InterruptsEnabled() = false;
		Cpu.EnabledInterrupts() = 0;
		Cpu.WaitingInterrupts() = 0;

		MemoryMap.SetBytes(0, { 0x10, IncB, IncA, 0x18, 0xfe });	// STOP; INC B; INC A; JR -2
		MemoryMap.WriteByte(JoypAddress, SelectDirections);

		// Leave DIV non-zero, to show whether STOP resets it
		Timer.RunCycles(0x1234);
	}

	void MakeInterruptPending()
	{
		Cpu.EnabledInterrupts() = InterruptFlags::TimerInt;
		Cpu.WaitingInterrupts() = InterruptFlags::TimerInt;
	}

	// Ends STOP mode and runs the instruction after STOP
	void WakeAndRunNextInstruction()
	{
		Joypad.SetKeysDown(JoypadKey::Right);
		Step(2);
	}

	unsigned char Div() { return MemoryMap.ReadByte(MemoryMap::DividerPort); }
};

TEST_F(CpuStopTests, WithNoButtonOrInterruptEntersStopModeAndResetsDiv)
{
	Step(1);
	EXPECT_EQ(CpuState::Stopped, Cpu.State());
	EXPECT_TRUE(Cpu.IsStopped());
	EXPECT_EQ(0, Div());
}

TEST_F(CpuStopTests, WithNoButtonOrInterruptIsATwoByteOpcode)
{
	Step(1);
	WakeAndRunNextInstruction();

	EXPECT_EQ(0, Cpu.Registers().B);
	EXPECT_EQ(1, Cpu.Registers().A);
}

TEST_F(CpuStopTests, WithAnInterruptPendingEntersStopModeAndResetsDiv)
{
	MakeInterruptPending();

	Step(1);
	EXPECT_EQ(CpuState::Stopped, Cpu.State());
	EXPECT_EQ(0, Div());
}

TEST_F(CpuStopTests, WithAnInterruptPendingIsAOneByteOpcode)
{
	MakeInterruptPending();

	Step(1);
	WakeAndRunNextInstruction();

	EXPECT_EQ(1, Cpu.Registers().B);
	EXPECT_EQ(0, Cpu.Registers().A);
}

TEST_F(CpuStopTests, WithASelectedButtonHeldEntersHaltModeAsATwoByteOpcode)
{
	Joypad.SetKeysDown(JoypadKey::Right);
	auto div = Div();

	// Reading the skipped byte takes a machine cycle
	EXPECT_EQ(2 * MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(CpuState::Halted, Cpu.State());
	EXPECT_EQ(2, Cpu.Registers().PC);
	EXPECT_EQ(div, Div());

	// An interrupt ends the halt as usual, and execution carries on after the skipped byte
	MakeInterruptPending();
	Step(1);
	EXPECT_EQ(CpuState::Running, Cpu.State());
	EXPECT_EQ(0, Cpu.Registers().B);
	EXPECT_EQ(1, Cpu.Registers().A);
}

TEST_F(CpuStopTests, WithASelectedButtonHeldAndAnInterruptPendingDoesNothingAsAOneByteOpcode)
{
	Joypad.SetKeysDown(JoypadKey::Right);
	MakeInterruptPending();
	auto div = Div();

	EXPECT_EQ(MachineCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(CpuState::Running, Cpu.State());
	EXPECT_EQ(div, Div());

	Step(1);
	EXPECT_EQ(1, Cpu.Registers().B);
}

TEST_F(CpuStopTests, AHeldButtonThatJoypDoesntSelectDoesntCount)
{
	Joypad.SetKeysDown(JoypadKey::A);

	Step(1);
	EXPECT_EQ(CpuState::Stopped, Cpu.State());
	EXPECT_EQ(0, Div());
}

TEST_F(CpuStopTests, AHeldButtonWithNothingSelectedDoesntCount)
{
	MemoryMap.WriteByte(JoypAddress, SelectNothing);
	Joypad.SetKeysDown(JoypadKey::Right);

	Step(1);
	EXPECT_EQ(CpuState::Stopped, Cpu.State());
}

TEST_F(CpuStopTests, TimerDoesntRunWhileStopped)
{
	MemoryMap.WriteByte(TacAddress, 0x05);	// Enabled, increments every 4 machine cycles
	MemoryMap.WriteByte(TimaAddress, 0x80);
	Step(1);
	auto tima = MemoryMap.ReadByte(TimaAddress);

	Step(10000);
	EXPECT_EQ(0, Div());
	EXPECT_EQ(tima, MemoryMap.ReadByte(TimaAddress));
	EXPECT_EQ(0, Cpu.WaitingInterrupts());
}

TEST_F(CpuStopTests, TimePassesWhileStoppedWithoutRunningTheSystemClock)
{
	Step(1);
	auto cycles = Hardware.Cycles;
	auto totalCycles = Cpu.GetTotalCycles();

	for (auto i = 0; i < 1000; i++) EXPECT_EQ(MachineCycle, Cpu.DoNextInstruction());

	EXPECT_EQ(cycles, Hardware.Cycles);
	EXPECT_EQ(1000 * MachineCycle, Hardware.StoppedCycles);
	EXPECT_EQ(totalCycles + 1000 * MachineCycle, Cpu.GetTotalCycles());
}

TEST_F(CpuStopTests, InterruptsDontEndStopMode)
{
	Step(1);
	Cpu.InterruptsEnabled() = true;
	Cpu.EnabledInterrupts() = InterruptFlags::AllInt;
	Cpu.RequestInterrupt(InterruptFlags::VBlankInt);
	Cpu.RequestInterrupt(InterruptFlags::JoypadInt);
	auto registers = Cpu.Registers();

	Step(100);
	EXPECT_EQ(CpuState::Stopped, Cpu.State());
	EXPECT_EQ(registers, Cpu.Registers());
}

TEST_F(CpuStopTests, PressingAButtonJoypDoesntSelectDoesntEndStopMode)
{
	Step(1);
	Joypad.SetKeysDown(JoypadKey::Start);

	Step(100);
	EXPECT_EQ(CpuState::Stopped, Cpu.State());
}

TEST_F(CpuStopTests, PressingASelectedButtonEndsStopMode)
{
	MemoryMap.WriteByte(JoypAddress, SelectButtons);
	Step(1);

	Joypad.SetKeysDown(JoypadKey::Start);
	Step(1);
	EXPECT_EQ(CpuState::Running, Cpu.State());
}

TEST_F(CpuStopTests, TimerRunsAgainAfterWaking)
{
	Step(1);
	WakeAndRunNextInstruction();

	// DIV increments every 64 machine cycles
	Step(64);
	EXPECT_NE(0, Div());
}

TEST_F(CpuStopTests, JoypadInterruptIsDispatchedAfterWakingWhenEnabled)
{
	Cpu.InterruptsEnabled() = true;
	Cpu.EnabledInterrupts() = InterruptFlags::JoypadInt;
	Step(1);

	Joypad.SetKeysDown(JoypadKey::Right);
	Step(2);
	EXPECT_EQ(0x60, Cpu.Registers().PC);
}

TEST_F(CpuStopTests, InterruptAfterWakingFromAOneByteStopReturnsToTheByteAfterStop)
{
	// EI takes effect after the instruction that follows it, so STOP runs with an interrupt pending, then IME is set
	MemoryMap.SetBytes(0, { 0xfb, 0x10, IncB, IncA });	// EI; STOP; INC B; INC A
	MakeInterruptPending();

	Step(2);
	ASSERT_EQ(CpuState::Stopped, Cpu.State());
	EXPECT_TRUE(Cpu.InterruptsEnabled());

	Joypad.SetKeysDown(JoypadKey::Right);
	Step(2);
	EXPECT_EQ(0x50, Cpu.Registers().PC);
	EXPECT_EQ(StackTop - 2, Cpu.Registers().SP);
	EXPECT_EQ(0x02, MemoryMap.ReadByte(StackTop - 2));
	EXPECT_EQ(0x00, MemoryMap.ReadByte(StackTop - 1));
}

TEST_F(CpuStopTests, DivResetIncrementsTimaLikeADivWrite)
{
	// Per https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html, resetting DIV while the divider bit that TAC selects
	// is set is a falling edge, which increments TIMA
	MemoryMap.WriteByte(MemoryMap::DividerPort, 0);
	MemoryMap.WriteByte(TacAddress, 0x05);	// Enabled, divider bit 3
	MemoryMap.WriteByte(TimaAddress, 0x80);
	Timer.RunCycles(8);						// STOP's opcode fetch takes the divider to 12: bit 3 still set

	Step(1);
	EXPECT_EQ(0x81, MemoryMap.ReadByte(TimaAddress));
}

TEST_F(CpuStopTests, DivResetStepsTheApuFrameSequencerLikeADivWrite)
{
	// Per https://gbdev.io/pandocs/Audio_details.html, resetting DIV while its bit 4 is set steps the frame
	// sequencer. Its first step clocks the length counters, so a channel with 1 step of length left stops
	ApuTest::ApuHarness apu;
	MemoryMap.SetApu(&apu.Device);
	apu.Write(ApuTest::Nr22, 0xf0);
	apu.Write(ApuTest::Nr21, 63);
	apu.Write(ApuTest::Nr24, 0xc0);
	apu.Device.RunCycles(4096);				// DIV bit 4 is bit 12 of the divider
	ASSERT_EQ(2, apu.Read(ApuTest::Nr52) & 2);

	Step(1);
	EXPECT_EQ(0, apu.Read(ApuTest::Nr52) & 2);
}

// Runs STOP through the whole emulator. The front end keeps calling GetFrame at the normal frame rate, so while
// stopped each call must still take a frame's worth of emulated time and produce that much audio
class EmulatorStopTests : public testing::Test
{
public:
	// Qualified with :: as the member is named after its type, which GCC otherwise rejects
	std::shared_ptr<TestCartridge> Rom{ std::make_shared<TestCartridge>() };
	::Emulator Emulator{ Rom };

	EmulatorStopTests()
	{
		// Disable the internal ROM so the program runs from address 0
		Emulator.GetMemoryMap().WriteByte(0xff50, 1);

		// With BGP mapping colour 0 to black, the blank tiles in VRAM make a black screen while the LCD is on
		Rom->SetBytes(0, {
			0x3e, SelectDirections,	// LD A,$20
			0xe0, 0x00,				// LDH (JOYP),A
			0x3e, 0xff,				// LD A,$FF
			0xe0, 0x47,				// LDH (BGP),A
			0x3e, 0x91,				// LD A,$91
			0xe0, 0x40,				// LDH (LCDC),A		LCD and background on
			0x10, 0x00,				// STOP
			0x18, 0xfe				// JR -2
		});
	}

	unsigned char ReadByte(unsigned short address) { return Emulator.GetMemoryMap().ReadByte(address); }

	static bool IsBlank(const int* frame)
	{
		return std::all_of(frame, frame + Graphics::HozPixels * Graphics::VertPixels, [](int pixel) { return pixel == -1; });
	}
};

TEST_F(EmulatorStopTests, ScreenIsBlankWhileStopped)
{
	Emulator.GetFrame();
	EXPECT_TRUE(IsBlank(Emulator.GetFrame()));
}

TEST_F(EmulatorStopTests, ScreenComesBackAfterWaking)
{
	Emulator.GetFrame();
	Emulator.GetJoypad().SetKeysDown(JoypadKey::Right);

	EXPECT_FALSE(IsBlank(Emulator.GetFrame()));
}

TEST_F(EmulatorStopTests, PpuDoesntRunWhileStopped)
{
	// STOP runs in mode 2 (OAM search) of line 0, and the PPU stays there. A running PPU would end each frame in
	// VBlank (mode 1)
	Emulator.GetFrame();
	auto ly = ReadByte(LyAddress);

	for (auto i = 0; i < 3; i++)
	{
		Emulator.GetFrame();
		EXPECT_EQ(ly, ReadByte(LyAddress));
		EXPECT_EQ(2, ReadByte(StatAddress) & 3);
	}
}

TEST_F(EmulatorStopTests, DivDoesntRunWhileStopped)
{
	for (auto i = 0; i < 3; i++)
	{
		Emulator.GetFrame();
		EXPECT_EQ(0, ReadByte(MemoryMap::DividerPort));
	}
}

TEST_F(EmulatorStopTests, AudioKeepsItsRateWhileStopped)
{
	std::vector<short> samples;
	Emulator.GetFrame();
	Emulator.TakeAudioSamples(samples);

	// A frame lasts 70224 cycles at 4194304 Hz: about 738.3 stereo samples at 44100 Hz
	for (auto i = 0; i < 3; i++)
	{
		Emulator.GetFrame();
		Emulator.TakeAudioSamples(samples);
		EXPECT_GE(samples.size(), 2u * 738);
		EXPECT_LE(samples.size(), 2u * 739);
	}
}

TEST_F(EmulatorStopTests, DivRunsAgainAfterWaking)
{
	Emulator.GetFrame();
	Emulator.GetJoypad().SetKeysDown(JoypadKey::Right);
	Emulator.GetFrame();

	EXPECT_NE(0, ReadByte(MemoryMap::DividerPort));
}

TEST_F(EmulatorStopTests, PpuCarriesOnFromItsLineOnWaking)
{
	// The program stops on line 50 and wakes at the start of a later frame. The first instruction still reads LY and
	// STAT as they were when the CPU stopped, and once the frame loop finishes its first line LY reads 51, as the
	// PPU carries on from where it stopped
	Rom->SetBytes(0, {
		0x3e, SelectDirections,	// LD A,$20
		0xe0, 0x00,				// LDH (JOYP),A
		0x3e, 0x91,				// LD A,$91
		0xe0, 0x40,				// LDH (LCDC),A		LCD and background on
		0xf0, 0x44,				// LDH A,(LY)		Wait for line 50
		0xfe, 50,				// CP 50
		0x20, 0xfa,				// JR NZ,-6
		0x10, 0x00,				// STOP
		0xf0, 0x44,				// LDH A,(LY)
		0xe0, 0x80,				// LDH ($80),A
		0xf0, 0x41,				// LDH A,(STAT)
		0xe0, 0x81,				// LDH ($81),A
		0xf0, 0x44,				// LDH A,(LY)		Wait for LY to change
		0xfe, 50,				// CP 50
		0x28, 0xfa,				// JR Z,-6
		0xe0, 0x82,				// LDH ($82),A
		0x18, 0xfe				// JR -2
	});

	Emulator.GetFrame();
	Emulator.GetFrame();
	Emulator.GetJoypad().SetKeysDown(JoypadKey::Right);
	Emulator.GetFrame();

	EXPECT_EQ(50, ReadByte(0xff80));
	EXPECT_EQ(2, ReadByte(0xff81) & 3);
	EXPECT_EQ(51, ReadByte(0xff82));
}
