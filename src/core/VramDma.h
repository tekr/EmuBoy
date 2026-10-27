#pragma once

class MemoryMap;
class Graphics;
class Cpu;
class StateWriter;
class StateReader;

// The CGB's VRAM DMA (HDMA1-HDMA5, $FF51-$FF55), which copies blocks of 16 bytes from ROM or RAM into the selected
// VRAM bank (https://gbdev.io/pandocs/CGB_Registers.html#lcd-vram-dma-transfers). A general-purpose transfer copies
// every block at once; an H-Blank transfer copies one block at the start of each H-Blank. The CPU waits while a block
// is copied, which Cpu::RunVramDma runs between an opcode fetch and its instruction.
class VramDma
{
public:
	static const unsigned short FirstRegister = 0xff51;
	static const unsigned short LastRegister = 0xff55;

	static const int BlockBytes = 16;
	static const int MaxBlocks = 128;

private:
	static const int SourceHigh = 0, SourceLow = 1, DestinationHigh = 2, DestinationLow = 3, Control = 4;
	static const unsigned char HBlankMode = 0x80;

	MemoryMap& _memoryMap;
	Graphics& _graphics;
	Cpu* _cpu{ nullptr };

	unsigned short _source{ 0 };

	// The whole 16-bit address HDMA3-4 give, of which bits 0-12 address VRAM (see Copy)
	unsigned short _destination{ 0 };

	// Blocks still to copy, including any being copied, which HDMA5 reads back less 1. 0 once a transfer completes
	int _blocksLeft{ 0 };

	// An H-Blank transfer is running: it copies a block at each H-Blank until its blocks run out or it's cancelled
	bool _hblankTransfer{ false };

	// Bytes the CPU is waiting for: a block of an H-Blank transfer, or the whole of a general-purpose one
	int _bytesToCopy{ 0 };

	// An H-Blank started while the CPU was halted, which holds the transfer back until it wakes (Pan Docs)
	bool _blockHeldByHalt{ false };

	unsigned char ReadSource(unsigned short address) const;
	void StartCopying(int bytes);
	void Finish();

public:
	VramDma(MemoryMap& memoryMap, Graphics& graphics) : _memoryMap(memoryMap), _graphics(graphics) {}

	void SetCpu(Cpu* cpu) { _cpu = cpu; }

	// As the CGB boot ROM leaves them: HDMA1-HDMA5 all $FF
	void ApplyCgbPostBootState();

	// No transfer, as at power-on
	void Reset();

	// Address is the full CPU address, $FF51-$FF55. The registers exist only in CGB mode, where MemoryMap passes them on
	unsigned char ReadRegister(unsigned short address) const;
	void WriteRegister(unsigned short address, unsigned char value);

	// Whether the CPU is waiting for bytes to be copied
	bool IsCopying() const { return _bytesToCopy > 0; }

	// Copies up to count bytes of what the CPU is waiting for
	void Copy(int count);

	// Called by the PPU as each H-Blank of lines 0-143 starts, and as the LCD is turned off outside H-Blank
	void HBlankStarted();

	// Called as the CPU wakes from HALT
	void CpuWoke();

	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);
};
