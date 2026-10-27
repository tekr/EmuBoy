#include "stdafx.h"
#include <gtest/gtest.h>
#include <algorithm>
#include "../core/StateStream.h"
#include "../core/SpriteManager.h"
#include "../core/Timer.h"
#include "TestCpu.h"
#include "TestGraphics.h"
#include "TestMemoryMap.h"

// Component state that only matters for a few clocks, or only in particular modes, which the test ROMs in
// SaveStateRoundTripTests.cpp are unlikely to be caught in when they save: the HALT bug, the timer's overflow and
// reload cycles, and the PPU's sprite size

namespace
{
	// Copies a component's state to another through a save state's writer and reader
	template <typename Component>
	void CopyState(const Component& from, Component& to)
	{
		StateWriter writer;
		from.WriteState(writer);

		StateReader reader{ writer.GetData() };
		to.ReadState(reader);
		EXPECT_TRUE(reader.AtEnd());
	}

	struct CpuRig
	{
		InputJoypad Joypad;
		TestMemoryMap Memory{ Joypad };
		TestCpu Processor{ Memory };

		CpuRig()
		{
			// HALT, INC A, INC A
			Memory.SetBytes(MemoryMap::RomFixed, { 0x76, 0x3c, 0x3c });
			Processor.EnabledInterrupts() = InterruptFlags::TimerInt;
		}
	};
}

TEST(CpuStateTest, RestoredJustAfterTheHaltBugStillReadsTheNextByteTwice)
{
	// HALT with IME clear and an interrupt pending doesn't halt, but the next opcode fetch doesn't increment PC
	// (Pan Docs, halt bug), so INC A runs twice
	CpuRig saved;
	saved.Processor.RequestInterrupt(InterruptFlags::TimerInt);
	saved.Processor.DoNextInstruction();

	CpuRig restored;
	CopyState(saved.Processor, restored.Processor);
	restored.Processor.DoNextInstruction();
	restored.Processor.DoNextInstruction();

	EXPECT_EQ(2, restored.Processor.GetRegisters().A);
	EXPECT_EQ(2, restored.Processor.GetRegisters().PC);
}

namespace
{
	const int CounterReg = 1;
	const int ModuloReg = 2;
	const int ControlReg = 3;

	// A timer with the CPU it interrupts
	struct TimerRig
	{
		InputJoypad Joypad;
		TestMemoryMap Memory{ Joypad };
		TestCpu Processor{ Memory };
		Timer DividerTimer;

		TimerRig()
		{
			DividerTimer.SetCpu(&Processor);
			Processor.WaitingInterrupts() = 0;
		}

		unsigned char Counter() const { return DividerTimer.ReadRegister(CounterReg); }
		bool TimerInterruptRequested() { return (Processor.WaitingInterrupts() & InterruptFlags::TimerInt) != 0; }
	};

	class TimerStateTest : public testing::Test
	{
	protected:
		TimerRig Saved;
		TimerRig Restored;

		// Counts every 16 clocks from $FF, so it overflows 16 clocks in, then is reloaded from TMA 4 clocks later
		// (Pan Docs, Timer Obscure Behaviour)
		TimerStateTest()
		{
			Saved.DividerTimer.WriteRegister(0, 0);
			Saved.DividerTimer.WriteRegister(ModuloReg, 0xf0);
			Saved.DividerTimer.WriteRegister(CounterReg, 0xff);
			Saved.DividerTimer.WriteRegister(ControlReg, 4 | 1);
		}
	};
}

TEST_F(TimerStateTest, RestoredBetweenOverflowAndReloadStillReloads)
{
	Saved.DividerTimer.RunCycles(16);
	ASSERT_EQ(0, Saved.Counter());

	CopyState(Saved.DividerTimer, Restored.DividerTimer);
	Restored.DividerTimer.RunCycles(4);

	EXPECT_EQ(0xf0, Restored.Counter());
	EXPECT_TRUE(Restored.TimerInterruptRequested());
}

TEST_F(TimerStateTest, RestoredInTheReloadCycleStillIgnoresCounterWrites)
{
	Saved.DividerTimer.RunCycles(20);
	ASSERT_EQ(0xf0, Saved.Counter());

	CopyState(Saved.DividerTimer, Restored.DividerTimer);
	Restored.DividerTimer.WriteRegister(CounterReg, 0x42);

	EXPECT_EQ(0xf0, Restored.Counter());
}

TEST_F(TimerStateTest, RestoredMidPeriodCountsAtTheSameTime)
{
	Saved.DividerTimer.RunCycles(7);

	CopyState(Saved.DividerTimer, Restored.DividerTimer);
	Restored.DividerTimer.RunCycles(8);
	EXPECT_EQ(0xff, Restored.Counter());
	Restored.DividerTimer.RunCycles(1);
	EXPECT_EQ(0, Restored.Counter());
}

namespace
{
	// The PPU with what it's connected to
	struct PpuRig
	{
		InputJoypad Joypad;
		TestMemoryMap Memory{ Joypad };
		TestCpu Processor{ Memory };
		SpriteManager Sprites;
		TestGraphics Ppu{ Processor, Memory, Sprites };

		void Run(int clocks)
		{
			for (auto clock = 0; clock < clocks; clock += 4) Ppu.RunCycles(4);
		}
	};
}

TEST(PpuStateTest, RestoresTheSpriteSize)
{
	const unsigned char lcdOnWithTallSprites = 0x80 | 0x04 | 0x02;
	const int spriteTop = 30;

	PpuRig saved;

	// A 8x16 sprite of colour 3, from tiles 0 and 1, written while the LCD is off
	for (unsigned short address = 0; address < 32; address++) saved.Ppu.WriteVram(address, 0xff);
	saved.Ppu.WriteOam(0, spriteTop + SpriteManager::SpriteYOffset);
	saved.Ppu.WriteOam(1, 50);
	saved.Ppu.WriteOam(2, 0);
	saved.Ppu.WriteRegister(TestGraphics::RegSprite0Palette, 0xe4);
	saved.Ppu.WriteRegister(TestGraphics::RegBgWinPalette, 0xe4);
	saved.Ppu.WriteRegister(TestGraphics::RegLcdControl, lcdOnWithTallSprites);

	// Saved above the sprite in the second frame (the first is blank), so all of it is drawn after restoring
	saved.Run(Graphics::FrameClocks + 20 * Graphics::ScanlineClocks);

	PpuRig restored;
	CopyState(saved.Ppu, restored.Ppu);

	saved.Run(Graphics::FrameClocks);
	restored.Run(Graphics::FrameClocks);

	auto bottomRow = restored.Ppu.Bitmap + (spriteTop + 15) * Graphics::HozPixels;
	EXPECT_EQ(Graphics::ShadeColour(3), bottomRow[50 - SpriteManager::SpriteXOffset]) << "The sprite's lower half isn't drawn";
	EXPECT_TRUE(std::equal(saved.Ppu.Bitmap, saved.Ppu.Bitmap + Graphics::HozPixels * Graphics::VertPixels, restored.Ppu.Bitmap));
}

TEST(PpuStateTest, RestoresSpritesWhereverTheRestoredPpuHadThem)
{
	// Sprites moved since the restored PPU's OAM was last written must be found where the state puts them
	PpuRig saved;
	PpuRig restored;
	for (unsigned short address = 0; address < 16; address++) saved.Ppu.WriteVram(address, 0xff);
	saved.Ppu.WriteOam(0, 40);
	saved.Ppu.WriteOam(1, 40);
	restored.Ppu.WriteOam(0, 100);
	restored.Ppu.WriteOam(1, 100);
	saved.Ppu.WriteRegister(TestGraphics::RegSprite0Palette, 0xe4);
	saved.Ppu.WriteRegister(TestGraphics::RegBgWinPalette, 0xe4);
	saved.Ppu.WriteRegister(TestGraphics::RegLcdControl, 0x80 | 0x02);
	saved.Run(Graphics::FrameClocks + Graphics::ScanlineClocks);

	CopyState(saved.Ppu, restored.Ppu);
	saved.Run(Graphics::FrameClocks);
	restored.Run(Graphics::FrameClocks);

	EXPECT_EQ(Graphics::ShadeColour(3), restored.Ppu.Bitmap[(40 - SpriteManager::SpriteYOffset) * Graphics::HozPixels + 40 - SpriteManager::SpriteXOffset]);
	EXPECT_TRUE(std::equal(saved.Ppu.Bitmap, saved.Ppu.Bitmap + Graphics::HozPixels * Graphics::VertPixels, restored.Ppu.Bitmap));
}

TEST(PpuStateTest, RestoresManySpritesOverOthers)
{
	// Restoring into a PPU whose OAM already holds other sprites replaces them all at once, which the sprite
	// manager's index must keep up with
	PpuRig saved;
	PpuRig restored;
	auto fillOam = [](PpuRig& rig, unsigned seed)
	{
		for (unsigned short address = 0; address < Graphics::OamSize; address++)
		{
			seed = seed * 1103515245 + 12345;
			auto value = static_cast<unsigned char>(seed >> 16);

			// Keep the sprites' Y on screen, so most lines have some
			rig.Ppu.WriteOam(address, address % 4 == 0 ? static_cast<unsigned char>(value % 160) : value);
		}
	};
	fillOam(saved, 1);
	fillOam(restored, 2);

	for (unsigned short address = 0; address < 256 * 16; address++) saved.Ppu.WriteVram(address, static_cast<unsigned char>(address * 7));
	saved.Ppu.WriteRegister(TestGraphics::RegSprite0Palette, 0xe4);
	saved.Ppu.WriteRegister(TestGraphics::RegSprite1Palette, 0x1b);
	saved.Ppu.WriteRegister(TestGraphics::RegBgWinPalette, 0xe4);
	saved.Ppu.WriteRegister(TestGraphics::RegLcdControl, 0x80 | 0x02);
	restored.Ppu.WriteRegister(TestGraphics::RegLcdControl, 0x80 | 0x02);
	restored.Run(Graphics::FrameClocks);
	saved.Run(Graphics::FrameClocks);

	CopyState(saved.Ppu, restored.Ppu);
	saved.Run(Graphics::FrameClocks);
	restored.Run(Graphics::FrameClocks);

	EXPECT_TRUE(std::equal(saved.Ppu.Bitmap, saved.Ppu.Bitmap + Graphics::HozPixels * Graphics::VertPixels, restored.Ppu.Bitmap));
}
