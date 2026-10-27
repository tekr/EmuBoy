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

// Save states taken at the moments where state hidden from the registers decides what happens next: between EI and the
// instruction after it, on the byte after the HALT bug, while halted, before and during OAM DMA, between TIMA
// overflowing and being reloaded, mid serial transfer, part-way through a second of the MBC3's clock, between the two
// writes that latch the clock, with the APU's length counters, envelopes and frame sequencer part-way through, and just
// after the APU is powered on with DIV's bit 4 set, and with the CPU stopped or locked up. Each test runs a small
// hand-assembled program instruction by instruction to the moment, found by what the program can see (PC, registers,
// clocks) rather than by the state itself, and checks the state holds the hidden value. It then restores the state into
// a newly powered-on emulator and runs both side by side, checking every instruction, frame, sound sample and serial
// byte comes out the same. The programs read back what they depend on, so leaving a value these moments depend on out
// of save states shows up as a difference. A few saved values can't affect anything once an instruction has finished,
// as the next machine cycle always replaces them: EI's flag for the instruction it precedes, the byte OAM DMA last read
// and the cycles left of TIMA's reload. SaveStateRoundTripTests.cpp does the same with test ROMs over longer runs.

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

	// A newly powered-on emulator, still running the boot ROM, as a state is restored into
	std::unique_ptr<Emulator> PowerOn(const std::vector<unsigned char>& rom)
	{
		return std::make_unique<Emulator>(CartridgeFactory::Create(std::vector<unsigned char>(rom)));
	}

	// A newly powered-on emulator that starts on the program at once. The boot ROM would need a valid header and take
	// 333 frames, so it hands over straight away, as it does by writing to $FF50
	std::unique_ptr<Emulator> PowerOnWithoutBootRom(std::shared_ptr<Cartridge> cartridge)
	{
		auto emulator = std::make_unique<Emulator>(cartridge);
		emulator->GetMemoryMap().WriteByte(MemoryMap::InternalRomDisable, 1);
		return emulator;
	}

	std::unique_ptr<Emulator> PowerOnWithoutBootRom(const std::vector<unsigned char>& rom)
	{
		return PowerOnWithoutBootRom(CartridgeFactory::Create(std::vector<unsigned char>(rom)));
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

	const Registers& CpuRegisters(const Emulator& emulator) { return emulator.GetCpu().GetRegisters(); }

	// The sections of a save state, without its header and CRC, which are quicker to make after every instruction
	std::vector<unsigned char> ComponentStates(const Emulator& emulator)
	{
		StateWriter writer;
		emulator.WriteState(writer);
		return writer.TakeData();
	}

	struct Section
	{
		std::string Tag;

		// Where its contents start in ComponentStates, and their length
		size_t Start;
		size_t Length;
	};

	std::vector<Section> Sections(const std::vector<unsigned char>& state)
	{
		std::vector<Section> sections;
		for (size_t offset = 0; offset + 8 <= state.size();)
		{
			auto length = state[offset + 4] | state[offset + 5] << 8 | state[offset + 6] << 16 | static_cast<size_t>(state[offset + 7]) << 24;
			sections.push_back({ std::string(state.begin() + offset, state.begin() + offset + 4), offset + 8, length });
			offset += 8 + length;
		}

		return sections;
	}

	// A value inside a section of the save state that no accessor shows, where the component's WriteState puts it. The
	// section's length is checked too, so a change to its layout fails here rather than reading another value
	struct HiddenValue
	{
		const char* Tag;
		size_t SectionLength;
		size_t Offset;
		size_t Size;
	};

	namespace HiddenValues
	{
		const size_t CpuSectionLength = 31;
		const size_t DmaSectionLength = 15;
		const size_t TimerSectionLength = 18;
		const size_t SerialSectionLength = 11;
		const size_t ApuSectionLength = 238;

		// After the six register pairs, the run state and the clock count
		const HiddenValue CpuSkipNextPcIncrement{ "CPU ", CpuSectionLength, 24, 1 };
		const HiddenValue CpuEnableInterruptsAfterNext{ "CPU ", CpuSectionLength, 26, 1 };

		// After the register
		const HiddenValue DmaStartDelay{ "DMA ", DmaSectionLength, 1, 4 };
		const HiddenValue DmaBytesTransferred{ "DMA ", DmaSectionLength, 10, 4 };

		// After the divider, the registers, the running flag and the mode
		const HiddenValue TimerCyclesToReload{ "TIMR", TimerSectionLength, 10, 4 };

		// After SB and SC
		const HiddenValue SerialBitsLeft{ "SERL", SerialSectionLength, 2, 4 };

		// After the sample rate, the registers, the power flag and the divider
		const HiddenValue ApuFrameStep{ "APU ", ApuSectionLength, 55, 4 };
		const HiddenValue ApuSkipFrameEvent{ "APU ", ApuSectionLength, 59, 1 };

		// Square 1's length counter, and its envelope's timer and running flag
		const HiddenValue ApuSquare1LengthCounter{ "APU ", ApuSectionLength, 65, 4 };
		const HiddenValue ApuSquare1EnvelopeTimer{ "APU ", ApuSectionLength, 83, 4 };
		const HiddenValue ApuSquare1EnvelopeRunning{ "APU ", ApuSectionLength, 92, 1 };

		// After the channels, the sweep and the sample phase, the clocks into the output sample being built
		const HiddenValue ApuSampleCycles{ "APU ", ApuSectionLength, 186, 4 };
	}

	// A little-endian hidden value
	uint32_t Read(const Emulator& emulator, const HiddenValue& value)
	{
		auto state = ComponentStates(emulator);
		for (auto& section : Sections(state))
		{
			if (section.Tag != value.Tag) continue;

			if (section.Length != value.SectionLength || value.Offset + value.Size > section.Length)
			{
				ADD_FAILURE() << "The " << value.Tag << " section is " << section.Length << " bytes rather than " << value.SectionLength <<
					"; its layout changed, so update HiddenValues";
				return 0;
			}

			uint32_t result = 0;
			for (size_t i = 0; i < value.Size; i++) result |= static_cast<uint32_t>(state[section.Start + value.Offset + i]) << (8 * i);
			return result;
		}

		ADD_FAILURE() << "No " << value.Tag << " section";
		return 0;
	}

	// How closely to compare the two emulators: their whole state after each of the first instructions, then at
	// the end of each frame
	const int InstructionsComparedInFull = 1000;

	// No frame takes more instructions than this, as each takes at least a machine cycle
	const int MaxInstructionsPerFrame = Graphics::FrameClocks / 4 + 1;

	testing::AssertionResult SameState(const Emulator& expected, const Emulator& actual, const std::string& when)
	{
		auto expectedState = ComponentStates(expected);
		auto actualState = ComponentStates(actual);
		if (expectedState == actualState) return testing::AssertionSuccess();

		size_t offset = 0;
		while (offset < expectedState.size() && offset < actualState.size() && expectedState[offset] == actualState[offset]) offset++;

		for (auto& section : Sections(expectedState))
		{
			if (offset >= section.Start && offset < section.Start + section.Length)
			{
				return testing::AssertionFailure() << "The states differ " << when << ", first in the " << section.Tag << " section at byte " <<
					offset - section.Start;
			}
		}

		return testing::AssertionFailure() << "The states differ " << when << ", first at byte " << offset;
	}

	// The keys held on each frame after saving
	using KeysHeld = std::function<JoypadKey(int frame)>;

	JoypadKey NoKeys(int) { return JoypadKey::NoKey; }

	// Runs the original emulator and the restored one side by side for some frames, checking they do the same
	testing::AssertionResult RunAlike(Emulator& original, Emulator& restored, int frames, const KeysHeld& keys)
	{
		std::string originalSerial, restoredSerial;
		original.GetSerial().SetByteSentHandler([&originalSerial](unsigned char byte) { originalSerial += static_cast<char>(byte); });
		restored.GetSerial().SetByteSentHandler([&restoredSerial](unsigned char byte) { restoredSerial += static_cast<char>(byte); });

		auto result = testing::AssertionSuccess();
		std::vector<short> originalSamples, restoredSamples;
		auto frame = 0;
		original.GetJoypad().SetKeysDown(keys(frame));
		restored.GetJoypad().SetKeysDown(keys(frame));
		for (auto instruction = 0; frame < frames; instruction++)
		{
			if (instruction == frames * MaxInstructionsPerFrame)
			{
				result = testing::AssertionFailure() << "Only " << frame << " of " << frames << " frames completed in " << instruction << " instructions";
				break;
			}

			auto originalFrameDone = original.RunInstruction();
			auto restoredFrameDone = restored.RunInstruction();
			auto when = "after instruction " + std::to_string(instruction) + " (frame " + std::to_string(frame) + ")";

			if (originalFrameDone != restoredFrameDone)
			{
				result = testing::AssertionFailure() << "Only one emulator completed a frame " << when;
				break;
			}

			if (original.GetSystemClocks() != restored.GetSystemClocks())
			{
				result = testing::AssertionFailure() << "The CPU clock counts differ " << when;
				break;
			}

			if (!(CpuRegisters(original) == CpuRegisters(restored)))
			{
				auto& expected = CpuRegisters(original);
				auto& actual = CpuRegisters(restored);
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
			original.GetJoypad().SetKeysDown(keys(frame));
			restored.GetJoypad().SetKeysDown(keys(frame));

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
	void ExpectRestoredCarriesOnAlike(Emulator& original, const std::vector<unsigned char>& rom, int frames, const KeysHeld& keys = NoKeys)
	{
		// Samples already produced go out before the save, so aren't in the state
		std::vector<short> samples;
		original.TakeAudioSamples(samples);

		auto state = SaveState::Create(original);
		auto restored = PowerOn(rom);
		ASSERT_EQ("", SaveState::Restore(*restored, state));
		EXPECT_TRUE(SameState(original, *restored, "straight after restoring"));
		EXPECT_TRUE(RunAlike(original, *restored, frames, keys));
	}
}

TEST(SaveStateHiddenStateTest, BetweenEiAndTheNextInstruction)
{
	// With a timer interrupt already requested, EI enables interrupts only after the next instruction, so INC B runs
	// once before the dispatch (Pan Docs, Interrupts)
	Program program;
	program.WriteIo(0xff, 0x04).WriteIo(0x0f, 0x04).Op({ 0xfb });	// IE = IF = timer; EI
	auto afterEi = program.Here();
	program.Op({ 0x04, 0x04 }).Loop();	// INC B; INC B

	auto rom = program.Rom();
	auto emulator = PowerOnWithoutBootRom(rom);
	ASSERT_TRUE(RunUntil(*emulator, PcIs(afterEi)));
	EXPECT_EQ(1u, Read(*emulator, HiddenValues::CpuEnableInterruptsAfterNext)) << "EI isn't waiting to take effect";

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);

	auto& registers = CpuRegisters(*emulator);
	EXPECT_EQ(0x50, registers.D) << "The timer interrupt wasn't dispatched";
	EXPECT_EQ(1, registers.E) << "Interrupts dispatched";
	EXPECT_EQ(2, registers.B);
}

TEST(SaveStateHiddenStateTest, OnTheByteAfterTheHaltBug)
{
	// HALT with IME clear and an interrupt pending doesn't halt, and the next opcode fetch doesn't increment PC, so
	// INC B runs twice (Pan Docs, halt)
	Program program;
	program.WriteIo(0xff, 0x04).WriteIo(0x0f, 0x04).Op({ 0x76 });	// IE = IF = timer; HALT
	auto afterHalt = program.Here();
	program.Op({ 0x04 }).Loop();	// INC B

	auto rom = program.Rom();
	auto emulator = PowerOnWithoutBootRom(rom);
	ASSERT_TRUE(RunUntil(*emulator, PcIs(afterHalt)));
	EXPECT_EQ(1u, Read(*emulator, HiddenValues::CpuSkipNextPcIncrement)) << "The HALT bug didn't strike";

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
	EXPECT_EQ(2, CpuRegisters(*emulator).B);
	EXPECT_EQ(0, CpuRegisters(*emulator).E) << "Interrupts dispatched with IME clear";
}

TEST(SaveStateHiddenStateTest, WhileHalted)
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
		auto emulator = PowerOnWithoutBootRom(rom);
		ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return !e.GetCpu().IsRunning(); }));
		for (auto cycle = 0; cycle < haltedCycles; cycle++) emulator->RunInstruction();
		ASSERT_FALSE(emulator->GetCpu().IsRunning());

		ExpectRestoredCarriesOnAlike(*emulator, rom, 3);
		EXPECT_EQ(0x50, CpuRegisters(*emulator).D) << "The timer interrupt didn't end the halt";
		EXPECT_GT(CpuRegisters(*emulator).B, 0);
	}
}

TEST(SaveStateHiddenStateTest, BeforeAndDuringOamDma)
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
	program.Op({ 0x21, 0x9f, 0xfe, 0x4e, 0xf0, 0x46, 0x57 }).Loop();	// LD HL,$FE9F; LD C,(HL); LDH A,($46); LD D,A

	auto rom = program.Rom();

	// Straight after the write to $FF46, then 50 NOPs (machine cycles) later
	for (auto nops : { 0, 50 })
	{
		SCOPED_TRACE(nops ? "Part-way through the transfer" : "Before the transfer starts");
		auto emulator = PowerOnWithoutBootRom(rom);
		ASSERT_TRUE(RunUntil(*emulator, PcIs(static_cast<unsigned short>(afterDmaWrite + nops))));
		if (nops)
		{
			EXPECT_TRUE(emulator->GetOamDma().IsTransferring());
			EXPECT_EQ(49u, Read(*emulator, HiddenValues::DmaBytesTransferred));
		}
		else
		{
			EXPECT_GT(Read(*emulator, HiddenValues::DmaStartDelay), 0u);
		}

		ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
		auto& registers = CpuRegisters(*emulator);
		EXPECT_EQ(static_cast<unsigned char>(0x11 + 7 * 0x9f), registers.C) << "OAM wasn't filled";
		EXPECT_NE(0x11, registers.B) << "Reading VRAM during the transfer wasn't blocked";
		EXPECT_EQ(0x80, registers.D);
	}
}

TEST(SaveStateHiddenStateTest, BetweenTimaOverflowingAndBeingReloaded)
{
	// TIMA counts every 16 clocks from $FD, reloading $80 from TMA and requesting the timer interrupt a machine cycle
	// after it overflows (Pan Docs, Timer obscure behaviour). Running NOPs, each machine cycle ends an instruction.
	// Interrupts stay disabled, as dispatching one would take the reload's machine cycle into the same step
	Program program;
	program.WriteIo(0x06, 0x80).WriteIo(0x05, 0xfd).WriteIo(0x07, 0x05);	// TMA; TIMA; TAC = 16 clocks
	auto sled = program.Here();
	program.Nops(40).JrTo(sled);

	auto rom = program.Rom();
	auto emulator = PowerOnWithoutBootRom(rom);

	// The machine cycle after the overflow, with TIMA reading 0. Saving in the reload's machine cycle checks nothing
	// more, as nothing can see the cycles left of it once an instruction has finished
	ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return (e.GetMemoryMap().ReadByte(0xff07) & 4) != 0 && e.GetMemoryMap().ReadByte(0xff05) == 0; }));
	EXPECT_GT(Read(*emulator, HiddenValues::TimerCyclesToReload), 0u);

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
	EXPECT_EQ(0x04, emulator->GetMemoryMap().ReadByte(0xff0f) & 0x04) << "The timer interrupt wasn't requested";
}

TEST(SaveStateHiddenStateTest, MidSerialTransfer)
{
	// Sends $5A on the internal clock, a bit every 512 clocks, then the serial interrupt, all the while reading SB
	// into C and SC into B
	Program program;
	program.WriteIo(0xff, 0x08).WriteIo(0x01, 0x5a).WriteIo(0x02, 0x81).Op({ 0xfb });	// IE = serial; SB; SC = start, internal clock; EI
	auto loop = program.Here();
	program.Op({ 0xf0, 0x01, 0x4f, 0xf0, 0x02, 0x47 }).Nops(40).JrTo(loop);	// LDH A,($01); LD C,A; LDH A,($02); LD B,A

	auto rom = program.Rom();
	auto emulator = PowerOnWithoutBootRom(rom);

	// Half-way through the transfer, with four 1s shifted into SB, and half-way through the next bit
	ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return e.GetMemoryMap().ReadByte(0xff01) == 0xaf; }));
	auto clocks = emulator->GetSystemClocks();
	ASSERT_TRUE(RunUntil(*emulator, [clocks](Emulator& e) { return e.GetSystemClocks() >= clocks + Serial::CyclesPerBit / 2; }));
	EXPECT_EQ(4u, Read(*emulator, HiddenValues::SerialBitsLeft));

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);

	// Nothing is connected, so 1s were shifted in
	auto& registers = CpuRegisters(*emulator);
	EXPECT_EQ(0xff, registers.C);
	EXPECT_EQ(0x7f, registers.B) << "The transfer didn't end";
	EXPECT_EQ(0x58, registers.D) << "The serial interrupt wasn't dispatched";
	EXPECT_EQ(1, registers.E) << "Interrupts dispatched";
}

namespace
{
	// MBC3 with the clock: sets it to 30 seconds, then keeps latching it and reading its seconds into B. Returns the
	// address of the write of 1 that latches it and of the read
	Program RealTimeClockProgram(unsigned short& latch, unsigned short& readSeconds)
	{
		Program program(0x0f);
		program.Write(0x0000, 0x0a).Write(0x4000, 0x08).Write(0xa000, 30);	// Enable RAM and the clock; select seconds; set them
		auto loop = program.Here();
		program.Write(0x6000, 0x00);
		latch = program.Here();
		program.Write(0x6000, 0x01);
		readSeconds = program.Here();
		program.Op({ 0xfa, 0x00, 0xa0, 0x47 }).JrTo(loop);	// LD A,($A000); LD B,A
		return program;
	}
}

TEST(SaveStateHiddenStateTest, PartWayThroughARealTimeClockSecond)
{
	unsigned short latch, readSeconds;
	auto rom = RealTimeClockProgram(latch, readSeconds).Rom();
	auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
	auto clock = cartridge->GetRealTimeClock();
	ASSERT_NE(nullptr, clock);
	auto emulator = PowerOnWithoutBootRom(cartridge);

	// Most of the way through a second, so the next tick comes a few frames after saving, between latching the
	// clock and reading it
	ASSERT_TRUE(RunUntil(*emulator, [clock, readSeconds](Emulator& e)
	{
		return clock->GetState().CyclesIntoSecond >= RealTimeClock::CyclesPerSecond * 7 / 8 && CpuRegisters(e).PC == readSeconds;
	}));
	EXPECT_EQ(30, CpuRegisters(*emulator).B);

	ExpectRestoredCarriesOnAlike(*emulator, rom, 12);
	EXPECT_EQ(31, CpuRegisters(*emulator).B) << "The clock didn't tick";
}

TEST(SaveStateHiddenStateTest, BetweenTheWritesThatLatchTheRealTimeClock)
{
	unsigned short latch, readSeconds;
	auto rom = RealTimeClockProgram(latch, readSeconds).Rom();
	auto cartridge = CartridgeFactory::Create(std::vector<unsigned char>(rom));
	auto clock = cartridge->GetRealTimeClock();
	ASSERT_NE(nullptr, clock);
	auto emulator = PowerOnWithoutBootRom(cartridge);

	// Writing 0 arms the latch and writing 1 then latches (Pan Docs, MBC3). Saving in between, just after the clock
	// has ticked past what was last latched, so latching changes what the program reads
	ASSERT_TRUE(RunUntil(*emulator, [clock, latch](Emulator& e)
	{
		return CpuRegisters(e).PC == latch && clock->GetState().Counters[RealTimeClock::Seconds] != clock->GetState().Latched[RealTimeClock::Seconds];
	}));
	EXPECT_TRUE(clock->GetState().LatchArmed);
	EXPECT_EQ(30, CpuRegisters(*emulator).B);

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
	EXPECT_EQ(31, CpuRegisters(*emulator).B) << "The clock wasn't latched";
}

TEST(SaveStateHiddenStateTest, PartWayThroughSoundLengthsAndEnvelopes)
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
		auto emulator = PowerOnWithoutBootRom(rom);
		ASSERT_TRUE(RunUntil(*emulator, [afterWrite](Emulator& e)
		{
			// The frame sequencer steps as DIV's bit 4 falls
			return e.GetSystemClocks() >= 150000 && (e.GetMemoryMap().ReadByte(0xff04) & 0x1f) == 0 && CpuRegisters(e).PC == afterWrite;
		}));
		for (auto instruction = 0; instruction < instructionsAfterStep; instruction++) emulator->RunInstruction();

		auto lengthLeft = Read(*emulator, HiddenValues::ApuSquare1LengthCounter);
		EXPECT_TRUE(lengthLeft > 0 && lengthLeft < 32) << lengthLeft;
		EXPECT_NE(0u, Read(*emulator, HiddenValues::ApuFrameStep));
		EXPECT_EQ(1u, Read(*emulator, HiddenValues::ApuSquare1EnvelopeRunning));
		EXPECT_GT(Read(*emulator, HiddenValues::ApuSquare1EnvelopeTimer), 0u);
		EXPECT_GT(Read(*emulator, HiddenValues::ApuSampleCycles), 0u);
		ASSERT_EQ(0x09, playing(*emulator));

		ExpectRestoredCarriesOnAlike(*emulator, rom, 20);
		EXPECT_EQ(0, playing(*emulator)) << "The length counters didn't stop the channels";
	}
}

TEST(SaveStateHiddenStateTest, AfterPoweringTheApuOnWithDivBit4Set)
{
	// Powering the APU on while DIV's bit 4 is set skips the frame sequencer's next step (SameBoy). Square 1 then
	// plays with a length of 1/256 s, while the program reads NR52 into B
	Program program;
	auto wait = program.Here();
	program.Op({ 0xf0, 0x04, 0xe6, 0x10, 0x28, static_cast<unsigned char>(wait - (program.Here() + 6)) });	// LDH A,(DIV); AND $10; JR Z,wait
	program.WriteIo(0x26, 0x80);	// NR52 on
	auto poweredOn = program.Here();
	program.WriteIo(0x24, 0x77).WriteIo(0x25, 0xff).WriteIo(0x11, 0x3f).WriteIo(0x12, 0xf0).WriteIo(0x14, 0xc7);	// Square 1
	auto loop = program.Here();
	program.Op({ 0xf0, 0x26, 0x47 }).JrTo(loop);	// LDH A,(NR52); LD B,A

	auto rom = program.Rom();
	auto emulator = PowerOnWithoutBootRom(rom);
	ASSERT_TRUE(RunUntil(*emulator, PcIs(poweredOn)));
	EXPECT_EQ(1u, Read(*emulator, HiddenValues::ApuSkipFrameEvent)) << "The frame sequencer isn't skipping its next step";

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
	EXPECT_EQ(0xf0, CpuRegisters(*emulator).B) << "The length counter didn't stop square 1";
}

TEST(SaveStateHiddenStateTest, WhileStopped)
{
	// With the LCD on and the buttons selected in JOYP, STOP stops the system clock until a button is pressed, then
	// the program counts in B (Pan Docs, Reducing power consumption). The timer runs, but stands still while stopped
	Program program;
	program.WriteIo(0x40, 0x91).WriteIo(0x00, 0x10).WriteIo(0x07, 0x05);	// LCDC on; JOYP selects the buttons; TAC = 16 clocks
	program.Op({ 0x10, 0x00 });	// STOP
	auto afterStop = program.Here();
	program.Op({ 0x04 }).JrTo(afterStop);	// INC B

	auto rom = program.Rom();

	// A, pressed two frames after saving
	auto pressA = [](int frame) { return frame >= 2 ? JoypadKey::A : JoypadKey::NoKey; };

	// Just stopped, then a frame and a half later, part-way through a stopped frame
	for (auto stoppedClocks : { 0u, Graphics::FrameClocks * 3 / 2 })
	{
		SCOPED_TRACE(std::to_string(stoppedClocks) + " clocks into STOP");
		auto emulator = PowerOnWithoutBootRom(rom);
		ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return e.GetCpu().IsStopped(); }));
		auto stoppedAt = emulator->GetSystemClocks();
		ASSERT_TRUE(RunUntil(*emulator, [stoppedAt, stoppedClocks](Emulator& e) { return e.GetSystemClocks() >= stoppedAt + stoppedClocks; }));
		ASSERT_TRUE(emulator->GetCpu().IsStopped());
		EXPECT_EQ(0, emulator->GetMemoryMap().ReadByte(0xff04)) << "STOP didn't reset DIV";

		ExpectRestoredCarriesOnAlike(*emulator, rom, 4, pressA);
		EXPECT_TRUE(emulator->GetCpu().IsRunning()) << "Pressing A didn't end STOP";
		EXPECT_GT(CpuRegisters(*emulator).B, 0);
	}
}

TEST(SaveStateHiddenStateTest, WhileLockedUp)
{
	// An unused opcode locks the CPU up for good, ignoring the timer interrupt, while the timer keeps counting
	// (Pan Docs, CPU instruction set)
	Program program;
	program.WriteIo(0xff, 0x04).WriteIo(0x07, 0x05).Op({ 0xfb });	// IE = timer; TAC = 16 clocks; EI
	program.Op({ 0xd3 });	// Unused
	program.Op({ 0x04 }).Loop();	// INC B

	auto rom = program.Rom();
	auto emulator = PowerOnWithoutBootRom(rom);
	ASSERT_TRUE(RunUntil(*emulator, [](Emulator& e) { return !e.GetCpu().IsRunning(); }));
	ASSERT_FALSE(emulator->GetCpu().IsStopped());
	auto lockedAt = CpuRegisters(*emulator).PC;

	ExpectRestoredCarriesOnAlike(*emulator, rom, 2);
	EXPECT_EQ(lockedAt, CpuRegisters(*emulator).PC);
	EXPECT_EQ(0, CpuRegisters(*emulator).E) << "Interrupts dispatched while locked up";
	EXPECT_EQ(0x04, emulator->GetMemoryMap().ReadByte(0xff0f) & 0x04) << "The timer stopped";
}
