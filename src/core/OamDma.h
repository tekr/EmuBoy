#pragma once

class MemoryMap;
class Graphics;
class StateWriter;
class StateReader;

// OAM DMA (https://gbdev.io/pandocs/OAM_DMA_Transfer.html): writing $XX to $FF46 copies $XX00-$XX9F to OAM, a byte
// per machine cycle. Timing per mooneye-test-suite's oam_dma_start, oam_dma_timing and oam_dma_restart
// (https://github.com/Gekkio/mooneye-test-suite): the transfer starts two machine cycles after the write and takes
// 160. Restarting it leaves the running transfer going until the new one starts.
class OamDma
{
	static const int TransferBytes = 160;
	static const int StartDelayCycles = 2;

	MemoryMap& _memoryMap;
	Graphics& _graphics;

	// The value last written, which $FF46 reads back
	unsigned char _register{ 0 };

	// Machine cycles until a requested transfer starts, or 0 if none is pending
	int _startDelay{ 0 };
	unsigned short _pendingSource{ 0 };

	bool _transferring{ false };
	unsigned short _source{ 0 };
	int _bytesTransferred{ 0 };

	// The byte the transfer last read, which the CPU reads instead if it uses the same bus
	unsigned char _busValue{ 0 };

public:
	OamDma(MemoryMap& memoryMap, Graphics& graphics) : _memoryMap(memoryMap), _graphics(graphics) {}

	unsigned char ReadRegister() const { return _register; }
	void WriteRegister(unsigned char value);

	// Runs one machine cycle
	void RunCycle()
	{
		if (_startDelay > 0 || _transferring) Step();
	}

	bool IsTransferring() const { return _transferring; }

	// While a transfer runs the CPU can't use OAM, nor the bus the transfer reads from (VRAM, or the external bus
	// for the cartridge and work RAM): it reads the byte being transferred and its writes are lost. It can use
	// HRAM, the I/O registers and the other bus as normal
	bool BlocksCpuAccess(unsigned short address) const
	{
		return _transferring && address < 0xff00 && (address >= 0xfe00 || IsVramBus(address) == IsVramBus(_source));
	}

	// What the CPU reads from an address that BlocksCpuAccess
	unsigned char BlockedRead(unsigned short address) const { return address >= 0xfe00 ? 0xff : _busValue; }

	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);

private:
	void Step();

	static bool IsVramBus(unsigned short address) { return address >= 0x8000 && address < 0xa000; }
};
