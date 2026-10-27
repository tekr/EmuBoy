#include "stdafx.h"
#include <gtest/gtest.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"
#include "../core/SaveState.h"

// Save states restore everything that affects what the emulator does next. Each test runs a test ROM (see
// roms/readme.md) to a series of save points, chosen to catch the emulator with as much state in flight as possible:
// mid-frame, halted, during OAM DMA or a serial transfer, with the LCD off, with sound playing, and so on. At each it
// saves a state, then records the frames, sound and serial output of the next frames. It restores the state into a
// newly powered-on emulator, and into the original one, and checks each gives exactly the same recording and ends in
// exactly the same state. Anything a state leaves out shows up as a difference sooner or later.

namespace
{
	const int FramesAfterSave = 30;

	// Loads the ROM with the RAM its header declares
	const std::optional<size_t> RamFromHeader = std::nullopt;
	const size_t OneRamBank = Cartridge::RamBankSize;

	struct Rom
	{
		std::string Path;

		// RAM in bytes for ROMs that use RAM without declaring it in their header, as blargg's do, or RamFromHeader
		std::optional<size_t> RamSize;

		// The Game Boy to run on, whatever the header asks for
		GameBoyModel Model = GameBoyModel::Dmg;
	};

	std::unique_ptr<Emulator> PowerOn(const Rom& rom)
	{
		auto cartridge = rom.RamSize ? CartridgeFactory::LoadFromFileWithRamSize(rom.Path, *rom.RamSize) : CartridgeFactory::LoadFromFile(rom.Path);
		return cartridge == nullptr ? nullptr : std::make_unique<Emulator>(cartridge, rom.Model);
	}

	// What the emulator puts out over some frames
	struct Recording
	{
		std::vector<std::vector<int>> Frames;
		std::vector<short> Audio;
		std::string Serial;
		Registers FinalRegisters;
	};

	// The keys held on each frame of a recording, so that the joypad's state is exercised too
	using InputScript = std::function<JoypadKey(int frame)>;

	JoypadKey PressSomeKeys(int frame)
	{
		static const JoypadKey keys[] = { JoypadKey::NoKey, JoypadKey::Down, JoypadKey::NoKey, JoypadKey::A,
			JoypadKey::NoKey, static_cast<JoypadKey>(JoypadKey::Start | JoypadKey::Left) };
		return keys[frame / 4 % (sizeof(keys) / sizeof(keys[0]))];
	}

	Recording Record(Emulator& emulator, int frames, const InputScript& input)
	{
		Recording recording;
		emulator.GetSerial().SetByteSentHandler([&recording](unsigned char byte) { recording.Serial += static_cast<char>(byte); });

		std::vector<short> samples;
		for (auto frame = 0; frame < frames; frame++)
		{
			emulator.GetJoypad().SetKeysDown(input(frame));

			auto bitmap = emulator.GetFrame();
			recording.Frames.emplace_back(bitmap, bitmap + Graphics::HozPixels * Graphics::VertPixels);

			emulator.TakeAudioSamples(samples);
			recording.Audio.insert(recording.Audio.end(), samples.begin(), samples.end());
		}

		emulator.GetSerial().SetByteSentHandler(nullptr);
		recording.FinalRegisters = emulator.GetCpu().GetRegisters();
		return recording;
	}

	testing::AssertionResult SameRecording(const Recording& expected, const Recording& actual)
	{
		for (size_t frame = 0; frame < expected.Frames.size(); frame++)
		{
			auto& expectedFrame = expected.Frames[frame];
			auto& actualFrame = actual.Frames[frame];

			for (size_t pixel = 0; pixel < expectedFrame.size(); pixel++)
			{
				if (expectedFrame[pixel] != actualFrame[pixel])
				{
					return testing::AssertionFailure() << "Frame " << frame << " differs first at (" <<
						pixel % Graphics::HozPixels << ", " << pixel / Graphics::HozPixels << ")";
				}
			}
		}

		if (expected.Audio.size() != actual.Audio.size())
		{
			return testing::AssertionFailure() << expected.Audio.size() << " audio samples rather than " << actual.Audio.size();
		}

		for (size_t sample = 0; sample < expected.Audio.size(); sample++)
		{
			if (expected.Audio[sample] != actual.Audio[sample])
			{
				return testing::AssertionFailure() << "Audio differs first at sample " << sample << " of " << expected.Audio.size();
			}
		}

		if (expected.Serial != actual.Serial)
		{
			return testing::AssertionFailure() << "Serial output \"" << actual.Serial << "\" rather than \"" << expected.Serial << "\"";
		}

		if (!(expected.FinalRegisters == actual.FinalRegisters))
		{
			return testing::AssertionFailure() << "The CPU ends with PC=" << std::hex << actual.FinalRegisters.PC << " rather than "
				<< expected.FinalRegisters.PC;
		}

		return testing::AssertionSuccess();
	}

	// Moves the emulator on to a save point. Returns false if it doesn't get there
	using Advance = std::function<bool(Emulator&)>;

	struct SavePoint
	{
		std::string Name;
		Advance AdvanceTo;
	};

	Advance Frames(int count)
	{
		return [count](Emulator& emulator)
		{
			for (auto frame = 0; frame < count; frame++) emulator.GetFrame();
			return true;
		};
	}

	Advance Instructions(int count)
	{
		return [count](Emulator& emulator)
		{
			for (auto instruction = 0; instruction < count; instruction++) emulator.RunInstruction();
			return true;
		};
	}

	// Holds keys for some frames, then lets go
	Advance Press(JoypadKey keys, int frames)
	{
		return [keys, frames](Emulator& emulator)
		{
			emulator.GetJoypad().SetKeysDown(keys);
			for (auto frame = 0; frame < frames; frame++) emulator.GetFrame();
			emulator.GetJoypad().SetKeysDown(JoypadKey::NoKey);
			return true;
		};
	}

	// Runs instruction by instruction until the condition holds, for up to maxFrames frames
	Advance Until(std::function<bool(Emulator&)> condition, int maxFrames = 600)
	{
		return [condition, maxFrames](Emulator& emulator)
		{
			for (auto frames = 0; frames < maxFrames;)
			{
				if (condition(emulator)) return true;
				if (emulator.RunInstruction()) frames++;
			}

			return false;
		};
	}

	unsigned char Read(Emulator& emulator, unsigned short address) { return emulator.GetMemoryMap().ReadByte(address); }

	bool LcdOn(Emulator& emulator) { return (Read(emulator, 0xff40) & 0x80) != 0; }
	bool LcdOff(Emulator& emulator) { return !LcdOn(emulator); }
	int Line(Emulator& emulator) { return Read(emulator, 0xff44); }
	int Mode(Emulator& emulator) { return Read(emulator, 0xff41) & 3; }
	bool Halted(Emulator& emulator) { return !emulator.GetCpu().IsRunning(); }
	bool OamDmaRunning(Emulator& emulator) { return emulator.GetOamDma().IsTransferring(); }
	bool SerialTransferring(Emulator& emulator) { return (emulator.GetSerial().ReadRegister(Serial::ControlReg) & 0x80) != 0; }

	// NR52's channel status bits
	bool ChannelsPlaying(Emulator& emulator, unsigned char channels) { return (Read(emulator, 0xff26) & channels) == channels; }

	SavePoint MidFrame(int line) { return { "Line " + std::to_string(line), Until([line](Emulator& e) { return LcdOn(e) && Line(e) == line; }) }; }
	SavePoint InMode(int mode) { return { "Mode " + std::to_string(mode), Until([mode](Emulator& e) { return LcdOn(e) && Mode(e) == mode; }) }; }

	// The boot ROM takes 333 frames to hand over to the cartridge
	const SavePoint AfterBoot{ "After the boot ROM", Until([](Emulator& e) { return !e.GetMemoryMap().InternalRomEnabled(); }) };

	const SavePoint WhileHalted{ "Halted", Until(Halted) };
	const SavePoint WithLcdOff{ "LCD off", Until(LcdOff) };
	const SavePoint DuringOamDma{ "During OAM DMA", Until(OamDmaRunning) };
	const SavePoint DuringSerialTransfer{ "During a serial transfer", Until(SerialTransferring) };

	// A value inside a section of the emulator's save state, for state no accessor shows
	uint32_t StateValue(Emulator& emulator, const std::string& tag, size_t offset, size_t size)
	{
		auto state = SaveState::Create(emulator);
		const size_t headerSize = 12;
		for (auto section = headerSize; section + 8 <= state.size();)
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

	unsigned short ProgramCounter(Emulator& emulator) { return emulator.GetCpu().GetRegisters().PC; }

	// Between EI and the instruction after it, which IME waits for (Pan Docs, Interrupts). The CPU's state holds
	// that flag after its six register pairs, its run state, its clock count, the HALT bug flag and IME
	bool InEiDelay(Emulator& emulator)
	{
		const size_t enableAfterNextOffset = 12 + 4 + 8 + 1 + 1;
		return Read(emulator, static_cast<unsigned short>(ProgramCounter(emulator) - 1)) == 0xfb &&
			StateValue(emulator, "CPU ", enableAfterNextOffset, 1) != 0;
	}

	// Between a write to $FF46 and the transfer starting two machine cycles later. The OAM DMA state holds the
	// cycles left after the register's value
	bool InOamDmaStartDelay(Emulator& emulator)
	{
		auto pc = ProgramCounter(emulator);
		auto wroteFf46 = (Read(emulator, static_cast<unsigned short>(pc - 2)) == 0xe0 && Read(emulator, static_cast<unsigned short>(pc - 1)) == 0x46) ||
			(Read(emulator, static_cast<unsigned short>(pc - 1)) == 0xe2 && emulator.GetCpu().GetRegisters().C == 0x46);
		return wroteFf46 && StateValue(emulator, "DMA ", 1, 4) > 0;
	}

	const SavePoint DuringEiDelay{ "Between EI and the next instruction", Until(InEiDelay) };
	const SavePoint DuringOamDmaStartDelay{ "Before OAM DMA starts", Until(InOamDmaStartDelay) };

	// Saves at each point in turn and checks restoring the state carries on exactly as the emulator did
	void ExpectRoundTrips(const Rom& rom, const std::vector<SavePoint>& points, const InputScript& input = PressSomeKeys)
	{
		auto emulator = PowerOn(rom);
		ASSERT_NE(nullptr, emulator) << rom.Path << " not found; run the tests from src/tests";

		for (auto& point : points)
		{
			SCOPED_TRACE(point.Name);
			ASSERT_TRUE(point.AdvanceTo(*emulator)) << "The save point wasn't reached";

			// Samples already produced go out before the save, so aren't in the state
			std::vector<short> samples;
			emulator->TakeAudioSamples(samples);

			auto state = SaveState::Create(*emulator);
			auto expected = Record(*emulator, FramesAfterSave, input);
			auto expectedEnd = SaveState::Create(*emulator);

			auto restored = PowerOn(rom);
			ASSERT_EQ("", SaveState::Restore(*restored, state));
			EXPECT_TRUE(state == SaveState::Create(*restored)) << "Saving straight after restoring gives a different state";
			EXPECT_TRUE(SameRecording(expected, Record(*restored, FramesAfterSave, input))) << "Restored into a new emulator";
			EXPECT_TRUE(expectedEnd == SaveState::Create(*restored)) << "Restored into a new emulator, it ends in a different state";

			// Going back to the state in the emulator that saved it
			ASSERT_EQ("", SaveState::Restore(*emulator, state));
			EXPECT_TRUE(SameRecording(expected, Record(*emulator, FramesAfterSave, input))) << "Restored into the same emulator";
			EXPECT_TRUE(expectedEnd == SaveState::Create(*emulator)) << "Restored into the same emulator, it ends in a different state";
		}
	}
}

TEST(SaveStateRoundTripTest, BlarggCpuInterrupts)
{
	ExpectRoundTrips({ "roms/cpu_instrs/02-interrupts.gb", OneRamBank },
		{ { "Boot ROM", Frames(1) }, WhileHalted, { "Mid-frame", Instructions(12345) }, MidFrame(100) });
}

TEST(SaveStateRoundTripTest, BlarggCpuInstructions)
{
	ExpectRoundTrips({ "roms/cpu_instrs/09-op r,r.gb", OneRamBank },
		{ { "Frame 20", Frames(20) }, DuringSerialTransfer, { "Mid-frame", Instructions(4321) }, { "Frame 150", Frames(90) } });
}

TEST(SaveStateRoundTripTest, BlarggHaltBug)
{
	ExpectRoundTrips({ "roms/halt_bug/halt_bug.gb", OneRamBank }, { WhileHalted, { "Mid-frame", Instructions(777) }, WhileHalted, MidFrame(10) });
}

TEST(SaveStateRoundTripTest, DmgAcid2)
{
	ExpectRoundTrips({ "roms/dmg-acid2/dmg-acid2.gb", RamFromHeader },
		{ { "Boot ROM", Frames(3) }, WithLcdOff, { "Frame 100", Frames(70) }, MidFrame(72), InMode(2), InMode(3), InMode(0), MidFrame(150) });
}

TEST(SaveStateRoundTripTest, BlarggSoundTrigger)
{
	ExpectRoundTrips({ "roms/dmg_sound/03-trigger.gb", OneRamBank },
		{ { "Boot ROM's sound", Frames(300) }, { "Square 2 playing", Until([](Emulator& e) { return ChannelsPlaying(e, 2); }) },
			{ "Mid-frame", Instructions(3000) }, { "Wave playing", Until([](Emulator& e) { return ChannelsPlaying(e, 4); }) },
			{ "Noise playing", Until([](Emulator& e) { return ChannelsPlaying(e, 8); }) } });
}

TEST(SaveStateRoundTripTest, BlarggSoundSweep)
{
	ExpectRoundTrips({ "roms/dmg_sound/05-sweep details.gb", OneRamBank },
		{ AfterBoot, { "Sweeping", Until([](Emulator& e) { return ChannelsPlaying(e, 1); }) }, { "Mid-frame", Instructions(5000) } });
}

TEST(SaveStateRoundTripTest, BlarggSoundWaveRead)
{
	ExpectRoundTrips({ "roms/dmg_sound/09-wave read while on.gb", OneRamBank },
		{ { "Wave playing", Until([](Emulator& e) { return ChannelsPlaying(e, 4); }) }, { "Mid-frame", Instructions(2500) } });
}

TEST(SaveStateRoundTripTest, BlarggSoundWaveTrigger)
{
	ExpectRoundTrips({ "roms/dmg_sound/10-wave trigger while on.gb", OneRamBank },
		{ { "Wave playing", Until([](Emulator& e) { return ChannelsPlaying(e, 4); }) }, { "Frame 60", Frames(30) } });
}

TEST(SaveStateRoundTripTest, BlarggSoundLengthDuringPower)
{
	ExpectRoundTrips({ "roms/dmg_sound/08-len ctr during power.gb", OneRamBank }, { AfterBoot, { "Frame 15", Frames(15) }, { "Mid-frame", Instructions(9999) } });
}

TEST(SaveStateRoundTripTest, BlarggOamBugCauses)
{
	ExpectRoundTrips({ "roms/oam_bug/2-causes.gb", OneRamBank }, { AfterBoot, { "Frame 10", Frames(10) }, InMode(2), { "Mid-frame", Instructions(1111) } });
}

TEST(SaveStateRoundTripTest, BlarggOamBugInstructionEffect)
{
	ExpectRoundTrips({ "roms/oam_bug/8-instr_effect.gb", OneRamBank }, { AfterBoot, InMode(2), { "Frame 40", Frames(10) }, InMode(2) });
}

TEST(SaveStateRoundTripTest, Rtc3TestClock)
{
	// rtc3test's menu, then its basic tests, which run the clock for several seconds
	ExpectRoundTrips({ "roms/rtc3test/rtc3test.gb", RamFromHeader },
		{ AfterBoot, { "Menu", Frames(60) }, { "Running the basic tests", Press(JoypadKey::A, 10) }, { "Mid-test", Instructions(54321) },
			{ "Later in the tests", Frames(200) } },
		[](int) { return JoypadKey::NoKey; });
}

TEST(SaveStateRoundTripTest, MooneyeOamDmaRestart)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/oam_dma_restart.gb", RamFromHeader },
		{ DuringOamDma, { "Later in the transfer", Instructions(20) } });
}

TEST(SaveStateRoundTripTest, MooneyeOamDmaSources)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/oam_dma/sources-GS.gb", RamFromHeader },
		{ DuringOamDma, { "Next transfer", Instructions(200) } });
}

TEST(SaveStateRoundTripTest, MooneyeStatIrqBlocking)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/ppu/stat_irq_blocking.gb", RamFromHeader }, { AfterBoot, InMode(2), InMode(0), MidFrame(150) });
}

TEST(SaveStateRoundTripTest, MooneyeHaltWithInterruptsDisabled)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/halt_ime0_ei.gb", RamFromHeader }, { WhileHalted, { "Still halted", Instructions(5) } });
}

TEST(SaveStateRoundTripTest, MooneyeSpriteTiming)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/ppu/intr_2_mode0_timing_sprites.gb", RamFromHeader },
		{ WithLcdOff, InMode(3), { "Mid-frame", Instructions(2000) } });
}

TEST(SaveStateRoundTripTest, MooneyeLcdOnTiming)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/ppu/lcdon_timing-GS.gb", RamFromHeader }, { WithLcdOff, { "LCD on", Until(LcdOn) } });
}

TEST(SaveStateRoundTripTest, MooneyeTimaReload)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/timer/tima_reload.gb", RamFromHeader },
		{ { "Timer running", Until([](Emulator& e) { return (Read(e, 0xff07) & 4) != 0; }) }, { "Counting", Instructions(25) } });
}

TEST(SaveStateRoundTripTest, MooneyeSerialClockAlignment)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/serial/boot_sclk_align-dmgABCmgb.gb", RamFromHeader }, { DuringSerialTransfer });
}

TEST(SaveStateRoundTripTest, MooneyeMbc1Ram)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/emulator-only/mbc1/ram_64kb.gb", RamFromHeader }, { AfterBoot, { "Testing", Instructions(30000) } });
}

TEST(SaveStateRoundTripTest, MooneyeMbc1Multicart)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/emulator-only/mbc1/multicart_rom_8Mb.gb", RamFromHeader }, { AfterBoot, { "Testing", Instructions(20000) } });
}

TEST(SaveStateRoundTripTest, MooneyeMbc2Ram)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/emulator-only/mbc2/ram.gb", RamFromHeader }, { AfterBoot, { "Testing", Instructions(25000) } });
}

TEST(SaveStateRoundTripTest, MooneyeMbc5Rom)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/emulator-only/mbc5/rom_8Mb.gb", RamFromHeader }, { AfterBoot, { "Testing", Instructions(15000) } });
}

TEST(SaveStateRoundTripTest, BlarggInterruptsBetweenEiAndTheNextInstruction)
{
	ExpectRoundTrips({ "roms/cpu_instrs/02-interrupts.gb", OneRamBank }, { DuringEiDelay });
}

TEST(SaveStateRoundTripTest, MooneyeEiSequenceBetweenEiAndTheNextInstruction)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/ei_sequence.gb", RamFromHeader }, { DuringEiDelay });
}

TEST(SaveStateRoundTripTest, MooneyeOamDmaBeforeTheTransferStarts)
{
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/oam_dma_restart.gb", RamFromHeader }, { DuringOamDmaStartDelay });
	ExpectRoundTrips({ "roms/mooneye-test-suite/acceptance/oam_dma/basic.gb", RamFromHeader }, { DuringOamDmaStartDelay });
}

