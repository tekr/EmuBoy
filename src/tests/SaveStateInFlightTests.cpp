#include "stdafx.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "../core/RealTimeClock.h"
#include "../core/SaveState.h"
#include "../core/StateStream.h"

// Save states taken at the moments where state hidden from the registers decides what happens next: between EI and
// the instruction after it, on the byte after the HALT bug, while halted, before and during OAM DMA, between TIMA
// overflowing and being reloaded, mid serial transfer, part-way through a second of the MBC3's clock and with the
// APU's length counters, envelopes and frame sequencer part-way through. Each test runs a small hand-assembled program
// instruction by instruction to the moment, found by what the program can see (PC, registers, clocks) rather than by
// the state itself, and checks the state holds the hidden state. It then restores the state into a newly powered-on
// emulator and runs both side by side, checking every instruction, frame, sound sample and serial byte comes out the
// same. The programs read back what they depend on, so leaving any of that state out of save states shows up as a
// difference. SaveStateRoundTripTests.cpp does the same with test ROMs over longer runs.

namespace
{
	const unsigned short ProgramStart = 0x150;

	// Assembles a program at ProgramStart, in a 32 KiB ROM whose interrupt handlers each load D with their vector,
	// count the interrupt in E and return, so that every dispatch shows in the registers
	class Program
	{
		std::vector<unsigned char> _code;
		unsigned char _cartridgeType;

	public:
		explicit Program(unsigned char cartridgeType = 0x00) : _cartridgeType(cartridgeType)
		{
			Op({ 0x31, 0xfe, 0xff });	// LD SP,$FFFE
		}

		// The address of the next instruction
		unsigned short Here() const { return static_cast<unsigned short>(ProgramStart + _code.size()); }

		Program& Op(std::initializer_list<unsigned char> bytes)
		{
			_code.insert(_code.end(), bytes);
			return *this;
		}

		// LD A,value; LDH ($FF00+reg),A
		Program& WriteIo(unsigned char reg, unsigned char value) { return Op({ 0x3e, value, 0xe0, reg }); }

		// LD A,value; LD (address),A
		Program& Write(unsigned short address, unsigned char value)
		{
			return Op({ 0x3e, value, 0xea, static_cast<unsigned char>(address), static_cast<unsigned char>(address >> 8) });
		}

		Program& Nops(int count)
		{
			_code.insert(_code.end(), count, 0x00);
			return *this;
		}

		// JR to an earlier address
		Program& JrTo(unsigned short target) { return Op({ 0x18, static_cast<unsigned char>(target - (Here() + 2)) }); }

		// JR $
		Program& Loop() { return JrTo(Here()); }

		std::vector<unsigned char> Rom() const
		{
			std::vector<unsigned char> rom(0x8000);
			rom[0] = 0xc3;	// JP ProgramStart
			rom[1] = ProgramStart & 0xff;
			rom[2] = ProgramStart >> 8;

			for (unsigned char vector = 0x40; vector <= 0x60; vector += 8)
			{
				const unsigned char handler[] = { 0x16, vector, 0x1c, 0xd9 };	// LD D,vector; INC E; RETI
				std::copy(std::begin(handler), std::end(handler), rom.begin() + vector);
			}

			rom[CartridgeFactory::CartridgeTypeHeaderOffset] = _cartridgeType;
			std::copy(_code.begin(), _code.end(), rom.begin() + ProgramStart);
			return rom;
		}
	};

	// A newly powered-on emulator for the ROM. The boot ROM would need a valid header and take 333 frames, so it
	// hands over at once, as it does by writing to $FF50
	std::unique_ptr<Emulator> PowerOn(std::shared_ptr<Cartridge> cartridge, bool skipBootRom)
	{
		auto emulator = std::make_unique<Emulator>(cartridge);
		if (skipBootRom) emulator->GetMemoryMap().WriteByte(MemoryMap::InternalRomDisable, 1);
		return emulator;
	}

	std::unique_ptr<Emulator> PowerOn(const std::vector<unsigned char>& rom, bool skipBootRom)
	{
		return PowerOn(CartridgeFactory::Create(std::vector<unsigned char>(rom)), skipBootRom);
	}

	using Condition = std::function<bool(Emulator&)>;

	// Runs instruction by instruction until the condition holds. Returns false if it doesn't within the limit
	bool RunUntil(Emulator& emulator, const Condition& condition, int maxInstructions = 2000000)
	{
		for (auto instruction = 0; instruction < maxInstructions; instruction++)
		{
			if (condition(emulator)) return true;
			emulator.RunInstruction();
		}

		return false;
	}

	Condition PcIs(unsigned short address)
	{
		return [address](Emulator& emulator) { return emulator.GetCpu().GetRegisters().PC == address; };
	}

	// The sections of a save state, without its header and CRC, which are quicker to make after every instruction
	std::vector<unsigned char> ComponentStates(const Emulator& emulator)
	{
		StateWriter writer;
		emulator.WriteState(writer);
		return writer.TakeData();
	}

	// A little-endian value inside a section of the emulator's save state, for state no accessor shows
	uint32_t StateValue(const Emulator& emulator, const std::string& tag, size_t offset, size_t size)
	{
		auto state = ComponentStates(emulator);
		for (size_t section = 0; section + 8 <= state.size();)
		{
			auto length = state[section + 4] | state[section + 5] << 8 | state[section + 6] << 16 | static_cast<uint32_t>(state[section + 7]) << 24;
			if (std::string(state.begin() + section, state.begin() + section + 4) == tag)
			{
				uint32_t value = 0;
				for (size_t i = 0; i < size; i++) value |= static_cast<uint32_t>(state[section + 8 + offset + i]) << (8 * i);
				return value;
			}

			section += 8 + length;
		}

		ADD_FAILURE() << "No " << tag << " section";
		return 0;
	}

	// Where Cpu, OamDma, Timer, Serial and Apu::WriteState put the values the tests look for
	namespace Offsets
	{
		// After the six register pairs, the run state and the clock count
		const size_t CpuSkipNextPcIncrement = 24;
		const size_t CpuEnableInterruptsAfterNext = 26;

		// After the register
		const size_t DmaStartDelay = 1;
		const size_t DmaTransferring = 7;
		const size_t DmaBytesTransferred = 10;

		// After the divider, the registers, the running flag and the mode
		const size_t TimerCyclesToReload = 10;
		const size_t TimerCyclesLeftInReload = 14;

		// After SB and SC
		const size_t SerialBitsLeft = 2;

		// After the sample rate and the registers
		const size_t ApuFrameStep = 55;

		// Square 1's length counter, and its envelope's timer and running flag
		const size_t ApuSquare1LengthCounter = 65;
		const size_t ApuSquare1EnvelopeTimer = 83;
		const size_t ApuSquare1EnvelopeRunning = 92;

		// After the channels, the sweep and the sample phase, the clocks into the output sample being built
		const size_t ApuSampleCycles = 186;
	}

	// How closely to compare the two emulators: their whole state after each of the first instructions, then at
	// the end of each frame
	const int InstructionsComparedInFull = 1000;

	testing::AssertionResult SameState(const Emulator& expected, const Emulator& actual, const std::string& when)
	{
		auto expectedState = ComponentStates(expected);
		auto actualState = ComponentStates(actual);
		if (expectedState == actualState) return testing::AssertionSuccess();

		size_t offset = 0;
		while (offset < expectedState.size() && offset < actualState.size() && expectedState[offset] == actualState[offset]) offset++;
		return testing::AssertionFailure() << "The states differ " << when << ", first at byte " << offset;
	}

	// Runs the original emulator and the restored one side by side for some frames, checking they do the same
	testing::AssertionResult RunAlike(Emulator& original, Emulator& restored, int frames)
	{
		std::string originalSerial, restoredSerial;
		original.GetSerial().SetByteSentHandler([&originalSerial](unsigned char byte) { originalSerial += static_cast<char>(byte); });
		restored.GetSerial().SetByteSentHandler([&restoredSerial](unsigned char byte) { restoredSerial += static_cast<char>(byte); });

		auto result = testing::AssertionSuccess();
		std::vector<short> originalSamples, restoredSamples;
		for (auto instruction = 0, frame = 0; frame < frames; instruction++)
		{
			auto originalFrameDone = original.RunInstruction();
			auto restoredFrameDone = restored.RunInstruction();
			auto when = "after instruction " + std::to_string(instruction) + " (frame " + std::to_string(frame) + ")";

			if (originalFrameDone != restoredFrameDone)
			{
				result = testing::AssertionFailure() << "Only one emulator completed a frame " << when;
				break;
			}

			if (original.GetCpuClocks() != restored.GetCpuClocks())
			{
				result = testing::AssertionFailure() << "The CPU clock counts differ " << when;
				break;
			}

			if (!(original.GetCpu().GetRegisters() == restored.GetCpu().GetRegisters()))
			{
				auto& expected = original.GetCpu().GetRegisters();
				auto& actual = restored.GetCpu().GetRegisters();
				result = testing::AssertionFailure() << "The CPU registers differ " << when << std::hex << ": AF BC DE HL SP PC are " <<
					actual.AF << " " << actual.BC << " " << actual.DE << " " << actual.HL << " " << actual.SP << " " << actual.PC << " rather than " <<
					expected.AF << " " << expected.BC << " " << expected.DE << " " << expected.HL << " " << expected.SP << " " << expected.PC;
				break;
			}

			if (instruction < InstructionsComparedInFull || originalFrameDone)
			{
				result = SameState(original, restored, when);
				if (!result) break;
			}

			if (!originalFrameDone) continue;

			frame++;
			if (!std::equal(original.GetScreen(), original.GetScreen() + Graphics::HozPixels * Graphics::VertPixels, restored.GetScreen()))
			{
				result = testing::AssertionFailure() << "The frame differs " << when;
				break;
			}

			original.TakeAudioSamples(originalSamples);
			restored.TakeAudioSamples(restoredSamples);
			if (originalSamples != restoredSamples)
			{
				result = testing::AssertionFailure() << "The sound differs " << when;
				break;
			}
		}

		if (result && originalSerial != restoredSerial)
		{
			result = testing::AssertionFailure() << "Serial output \"" << restoredSerial << "\" rather than \"" << originalSerial << "\"";
		}

		original.GetSerial().SetByteSentHandler(nullptr);
		restored.GetSerial().SetByteSentHandler(nullptr);
		return result;
	}

	// Saves a state now, restores it into a newly powered-on emulator and checks the two carry on the same. The
	// original runs on, so its registers show what happened next
	void ExpectRestoredCarriesOnAlike(Emulator& original, const std::vector<unsigned char>& rom, int frames)
	{
		// Samples already produced go out before the save, so aren't in the state
		std::vector<short> samples;
		original.TakeAudioSamples(samples);

		auto state = SaveState::Create(original);
		auto restored = PowerOn(rom, false);
		ASSERT_EQ("", SaveState::Restore(*restored, state));
		EXPECT_TRUE(SameState(original, *restored, "straight after restoring"));
		EXPECT_TRUE(RunAlike(original, *restored, frames));
	}
}

TEST(SaveStateInFlightTest, BetweenEiAndTheNextInstruction)
{
	// With a timer interrupt already requested, EI enables interrupts only after the next instruction, so INC B runs
	// once before the dispatch (Pan Docs, Interrupts)
	Program program;
	program.WriteIo(0xff, 0x04).WriteIo(0x0f, 0x04).Op({ 0xfb });	// IE = IF = timer; EI
	auto afterEi = program.Here();
	program.Op({ 0x04, 0x04 }).Loop();	// INC B; INC B

	auto rom = program.Rom();
	auto emulator = PowerOn(rom, true);
	ASSERT_TRUE(RunUntil(*emulator, PcIs(afterEi)));
	EXPECT_EQ(1u, StateValue(*emulator, "CPU ", Offsets::CpuEnableInterruptsAfterNext, 1)) << "EI isn't waiting to take effect";

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);

	auto& registers = emulator->GetCpu().GetRegisters();
	EXPECT_EQ(0x50, registers.D) << "The timer interrupt wasn't dispatched";
	EXPECT_EQ(2, registers.B);
}

TEST(SaveStateInFlightTest, OnTheByteAfterTheHaltBug)
{
	// HALT with IME clear and an interrupt pending doesn't halt, and the next opcode fetch doesn't increment PC, so
	// INC B runs twice (Pan Docs, halt)
	Program program;
	program.WriteIo(0xff, 0x04).WriteIo(0x0f, 0x04).Op({ 0x76 });	// IE = IF = timer; HALT
	auto afterHalt = program.Here();
	program.Op({ 0x04 }).Loop();	// INC B

	auto rom = program.Rom();
	auto emulator = PowerOn(rom, true);
	ASSERT_TRUE(RunUntil(*emulator, PcIs(afterHalt)));
	EXPECT_EQ(1u, StateValue(*emulator, "CPU ", Offsets::CpuSkipNextPcIncrement, 1)) << "The HALT bug didn't strike";

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
	EXPECT_EQ(2, emulator->GetCpu().GetRegisters().B);
}

TEST(SaveStateInFlightTest, WhileHalted)
{
	// Halted until TIMA overflows, 64 machine cycles after the timer starts, then the handler runs and INC B
	Program program;
	program.WriteIo(0xff, 0x04).WriteIo(0x06, 0x00).WriteIo(0x05, 0xf0).WriteIo(0x07, 0x05);	// IE = timer; TMA = 0; TIMA = $F0; TAC = 16 clocks
	program.Op({ 0xfb });	// EI
	auto halt = program.Here();
	program.Op({ 0x76, 0x04 }).JrTo(halt);	// HALT; INC B

	auto rom = program.Rom();

	// Just halted, then part-way through
	for (auto haltedCycles : { 0, 30 })
	{
		SCOPED_TRACE(std::to_string(haltedCycles) + " machine cycles into the halt");
		auto emulator = PowerOn(rom, true);
		ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return !e.GetCpu().IsClockRunning(); }));
		for (auto cycle = 0; cycle < haltedCycles; cycle++) emulator->RunInstruction();
		ASSERT_FALSE(emulator->GetCpu().IsClockRunning());

		ExpectRestoredCarriesOnAlike(*emulator, rom, 3);
		EXPECT_EQ(0x50, emulator->GetCpu().GetRegisters().D) << "The timer interrupt didn't end the halt";
		EXPECT_GT(emulator->GetCpu().GetRegisters().B, 0);
	}
}

TEST(SaveStateInFlightTest, BeforeAndDuringOamDma)
{
	// Fills $8000-$809F, copies it to OAM by DMA while reading VRAM, which the transfer blocks, then reads OAM and
	// $FF46 back. The program runs from ROM, which the transfer from VRAM leaves the CPU free to use
	Program program;
	program.Op({ 0x21, 0x00, 0x80, 0x06, 0xa0, 0x3e, 0x11 });	// LD HL,$8000; LD B,$A0; LD A,$11
	auto fill = program.Here();
	program.Op({ 0x22, 0xc6, 0x07, 0x05, 0x20, static_cast<unsigned char>(fill - (program.Here() + 6)) });	// LD (HL+),A; ADD A,7; DEC B; JR NZ,fill
	program.Op({ 0x21, 0x00, 0x80 }).WriteIo(0x46, 0x80);	// LD HL,$8000; DMA from $8000
	auto afterDmaWrite = program.Here();
	program.Nops(80).Op({ 0x7e, 0x47 }).Nops(100);	// LD A,(HL); LD B,A
	program.Op({ 0x21, 0x9f, 0xfe, 0x4e, 0xf0, 0x46, 0x5f }).Loop();	// LD HL,$FE9F; LD C,(HL); LDH A,($46); LD E,A

	auto rom = program.Rom();

	// Straight after the write to $FF46, then 50 NOPs (machine cycles) later
	for (auto nops : { 0, 50 })
	{
		SCOPED_TRACE(nops ? "Part-way through the transfer" : "Before the transfer starts");
		auto emulator = PowerOn(rom, true);
		ASSERT_TRUE(RunUntil(*emulator, PcIs(static_cast<unsigned short>(afterDmaWrite + nops))));
		if (nops)
		{
			EXPECT_EQ(1u, StateValue(*emulator, "DMA ", Offsets::DmaTransferring, 1));
			EXPECT_EQ(49u, StateValue(*emulator, "DMA ", Offsets::DmaBytesTransferred, 4));
		}
		else
		{
			EXPECT_GT(StateValue(*emulator, "DMA ", Offsets::DmaStartDelay, 4), 0u);
		}

		ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
		auto& registers = emulator->GetCpu().GetRegisters();
		EXPECT_EQ(static_cast<unsigned char>(0x11 + 7 * 0x9f), registers.C) << "OAM wasn't filled";
		EXPECT_NE(0x11, registers.B) << "Reading VRAM during the transfer wasn't blocked";
		EXPECT_EQ(0x80, registers.E);
	}
}

TEST(SaveStateInFlightTest, BetweenTimaOverflowingAndBeingReloaded)
{
	// TIMA counts every 16 clocks from $FD, reloading $80 from TMA and requesting the timer interrupt a machine cycle
	// after it overflows (Pan Docs, Timer obscure behaviour). Running NOPs, each machine cycle ends an instruction.
	// Interrupts stay disabled, as dispatching one would take the reload's machine cycle into the same step
	Program program;
	program.WriteIo(0x06, 0x80).WriteIo(0x05, 0xfd).WriteIo(0x07, 0x05);	// TMA; TIMA; TAC = 16 clocks
	auto sled = program.Here();
	program.Nops(40).JrTo(sled);

	auto rom = program.Rom();

	// The machine cycle after the overflow, with TIMA reading 0, then the one it's reloaded in
	for (auto inReload : { false, true })
	{
		SCOPED_TRACE(inReload ? "In the reload" : "Between the overflow and the reload");
		auto emulator = PowerOn(rom, true);
		ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return (e.GetMemoryMap().ReadByte(0xff07) & 4) != 0 && e.GetMemoryMap().ReadByte(0xff05) == 0; }));
		EXPECT_GT(StateValue(*emulator, "TIMR", Offsets::TimerCyclesToReload, 4), 0u);

		if (inReload)
		{
			emulator->RunInstruction();
			EXPECT_GT(StateValue(*emulator, "TIMR", Offsets::TimerCyclesLeftInReload, 4), 0u);
			EXPECT_EQ(0x80, emulator->GetMemoryMap().ReadByte(0xff05));
		}

		ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
		EXPECT_EQ(0x04, emulator->GetMemoryMap().ReadByte(0xff0f) & 0x04) << "The timer interrupt wasn't requested";
	}
}

TEST(SaveStateInFlightTest, MidSerialTransfer)
{
	// Sends $5A on the internal clock, a bit every 512 clocks, then the serial interrupt, all the while reading SB
	// into C and SC into B
	Program program;
	program.WriteIo(0xff, 0x08).WriteIo(0x01, 0x5a).WriteIo(0x02, 0x81).Op({ 0xfb });	// IE = serial; SB; SC = start, internal clock; EI
	auto loop = program.Here();
	program.Op({ 0xf0, 0x01, 0x4f, 0xf0, 0x02, 0x47 }).Nops(40).JrTo(loop);	// LDH A,($01); LD C,A; LDH A,($02); LD B,A

	auto rom = program.Rom();
	auto emulator = PowerOn(rom, true);

	// Half-way through the transfer, with four 1s shifted into SB, and half-way through the next bit
	ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return e.GetMemoryMap().ReadByte(0xff01) == 0xaf; }));
	auto clocks = emulator->GetCpuClocks();
	ASSERT_TRUE(RunUntil(*emulator, [clocks](Emulator& e) { return e.GetCpuClocks() >= clocks + Serial::CyclesPerBit / 2; }));
	EXPECT_EQ(4u, StateValue(*emulator, "SERL", Offsets::SerialBitsLeft, 4));

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);

	// Nothing is connected, so 1s were shifted in
	auto& registers = emulator->GetCpu().GetRegisters();
	EXPECT_EQ(0xff, registers.C);
	EXPECT_EQ(0x7f, registers.B) << "The transfer didn't end";
	EXPECT_EQ(0x58, registers.D) << "The serial interrupt wasn't dispatched";
}

TEST(SaveStateInFlightTest, PartWayThroughARealTimeClockSecond)
{
	// MBC3 with the clock: sets it to 30 seconds, then keeps latching it and reading its seconds into B
	Program program(0x0f);
	program.Write(0x0000, 0x0a).Write(0x4000, 0x08).Write(0xa000, 30);	// Enable RAM and the clock; select seconds; set them
	auto loop = program.Here();
	program.Write(0x6000, 0x00).Write(0x6000, 0x01);	// Latch
	auto readSeconds = program.Here();
	program.Op({ 0xfa, 0x00, 0xa0, 0x47 }).JrTo(loop);	// LD A,($A000); LD B,A

	auto rom = program.Rom();
	auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
	auto clock = cartridge->GetRealTimeClock();
	ASSERT_NE(nullptr, clock);
	auto emulator = PowerOn(cartridge, true);

	// Most of the way through a second, so the next tick comes a few frames after saving, between latching the
	// clock and reading it
	ASSERT_TRUE(RunUntil(*emulator, [clock, readSeconds](Emulator& e)
	{
		return clock->GetState().CyclesIntoSecond >= RealTimeClock::CyclesPerSecond * 7 / 8 && e.GetCpu().GetRegisters().PC == readSeconds;
	}));
	EXPECT_EQ(30, emulator->GetCpu().GetRegisters().B);

	ExpectRestoredCarriesOnAlike(*emulator, rom, 12);
	EXPECT_EQ(31, emulator->GetCpu().GetRegisters().B) << "The clock didn't tick";
}

TEST(SaveStateInFlightTest, PartWayThroughSoundLengthsAndEnvelopes)
{
	// Square 1 for 32/256 s fading every 3/64 s, and noise for 64/256 s growing every 2/64 s, with their length
	// counters on (Pan Docs, Audio details)
	Program program;
	program.WriteIo(0x26, 0x80).WriteIo(0x24, 0x77).WriteIo(0x25, 0xff);	// NR52 on; NR50, NR51 full
	program.WriteIo(0x11, 0xa0).WriteIo(0x12, 0xf3).WriteIo(0x13, 0x00).WriteIo(0x14, 0xc7);	// Square 1
	program.WriteIo(0x20, 0x00).WriteIo(0x21, 0x2a).WriteIo(0x22, 0x35).WriteIo(0x23, 0xc0);	// Noise

	// Keeps writing NR51, which brings the APU up to date part-way through building an output sample
	program.Op({ 0x3e, 0xff });	// LD A,$FF
	auto loop = program.Here();
	program.Op({ 0xe0, 0x25 });	// LDH ($25),A
	auto afterWrite = program.Here();
	program.JrTo(loop);

	auto rom = program.Rom();
	auto playing = [](Emulator& e) { return e.GetMemoryMap().ReadByte(0xff26) & 0x0f; };

	// Once the envelopes have stepped and the length counters have counted down some (after two and a bit frames),
	// just after a frame sequencer step, then half-way to the next (8192 clocks apart; a time round the loop takes
	// 24), each time straight after the write
	for (auto instructionsAfterStep : { 0, 2 * (4096 / 24) })
	{
		SCOPED_TRACE(std::to_string(instructionsAfterStep) + " instructions after a frame sequencer step");
		auto emulator = PowerOn(rom, true);
		ASSERT_TRUE(RunUntil(*emulator, [afterWrite](Emulator& e)
		{
			// The frame sequencer steps as DIV's bit 4 falls
			return e.GetCpuClocks() >= 150000 && (e.GetMemoryMap().ReadByte(0xff04) & 0x1f) == 0 && e.GetCpu().GetRegisters().PC == afterWrite;
		}));
		for (auto instruction = 0; instruction < instructionsAfterStep; instruction++) emulator->RunInstruction();

		auto lengthLeft = StateValue(*emulator, "APU ", Offsets::ApuSquare1LengthCounter, 4);
		EXPECT_TRUE(lengthLeft > 0 && lengthLeft < 32) << lengthLeft;
		EXPECT_NE(0u, StateValue(*emulator, "APU ", Offsets::ApuFrameStep, 4));
		EXPECT_EQ(1u, StateValue(*emulator, "APU ", Offsets::ApuSquare1EnvelopeRunning, 1));
		EXPECT_GT(StateValue(*emulator, "APU ", Offsets::ApuSquare1EnvelopeTimer, 4), 0u);
		EXPECT_GT(StateValue(*emulator, "APU ", Offsets::ApuSampleCycles, 4), 0u);
		ASSERT_EQ(0x09, playing(*emulator));

		ExpectRestoredCarriesOnAlike(*emulator, rom, 20);
		EXPECT_EQ(0, playing(*emulator)) << "The length counters didn't stop the channels";
	}
}
