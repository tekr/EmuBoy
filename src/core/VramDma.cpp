#include "stdafx.h"
#include "VramDma.h"
#include "MemoryMap.h"
#include "Graphics.h"
#include "Cpu.h"
#include "StateStream.h"

void VramDma::ApplyCgbPostBootState()
{
	Reset();
	_source = 0xfff0;
	_destination = 0xfff0;
}

void VramDma::Reset()
{
	_source = 0;
	_destination = 0;
	_blocksLeft = 0;
	_hblankTransfer = false;
	_bytesToCopy = 0;
	_blockHeldByHalt = false;
}

unsigned char VramDma::ReadRegister(unsigned short address) const
{
	// HDMA1-HDMA4 are write-only. HDMA5's bit 7 reads 0 while a transfer is running, and the rest the blocks left
	// less 1, so $FF once a transfer completes
	if (address - FirstRegister != Control) return 0xff;

	auto running = _hblankTransfer || _bytesToCopy > 0;
	return (running ? 0 : 0x80) | ((_blocksLeft - 1) & 0x7f);
}

void VramDma::WriteRegister(unsigned short address, unsigned char value)
{
	// The low 4 bits of each address are ignored. The destination's top 3 bits don't select where in VRAM it copies to,
	// but are kept, as a transfer stops when the whole address passes $FFFF (see Copy)
	switch (address - FirstRegister)
	{
	case SourceHigh:
		_source = static_cast<unsigned short>((_source & 0x00f0) | value << 8);
		return;

	case SourceLow:
		_source = static_cast<unsigned short>((_source & 0xff00) | (value & 0xf0));
		return;

	case DestinationHigh:
		_destination = static_cast<unsigned short>((_destination & 0x00f0) | value << 8);
		return;

	case DestinationLow:
		_destination = static_cast<unsigned short>((_destination & 0xff00) | (value & 0xf0));
		return;

	default:
		break;
	}

	// Writing bit 7 clear during an H-Blank transfer cancels it. Per Pan Docs, HDMA5 then reads the blocks that were
	// left, with bit 7 set. SameBoy and Gambatte instead load the length just written, so read $80 after writing $00;
	// no hardware test settles it, and games test only bit 7, on which all three agree
	if (!(value & HBlankMode) && _hblankTransfer)
	{
		_hblankTransfer = false;
		return;
	}

	_blocksLeft = (value & 0x7f) + 1;

	if (value & HBlankMode)
	{
		// Started during an H-Blank (or with the LCD off), the first block is copied straight away (SameBoy)
		_hblankTransfer = true;
		if (_graphics.InHBlankForVramDma()) StartCopying(BlockBytes);
	}
	else
	{
		StartCopying(_blocksLeft * BlockBytes);
	}
}

void VramDma::StartCopying(int bytes)
{
	_bytesToCopy = bytes;
	_cpu->RequestVramDma();
}

unsigned char VramDma::ReadSource(unsigned short address) const
{
	// Per Pan Docs the source should be ROM, cartridge RAM or work RAM; from VRAM it copies garbage. As in SameBoy,
	// only those areas are read, and anything else copies $FF
	if (address < 0x8000 || (address >= 0xa000 && address < 0xe000)) return _memoryMap.ReadByteForDma(address);
	return 0xff;
}

void VramDma::Copy(int count)
{
	for (; count > 0 && _bytesToCopy > 0; count--)
	{
		// The destination's low 13 bits address VRAM, so it wraps from $9FFF to $8000 (for HDMA3 $9F, say). A transfer
		// stops early only when the whole address passes $FFFF, as Gambatte's hardware tests dma_dst_wrap_1 and
		// dma_dst_wrap_2 show (HDMA3 $DF wraps to $8000; $FF stops), and as SameBoy and Gambatte do. Pan Docs says
		// only that a transfer stops if the destination overflows
		_graphics.WriteVramForDma(_destination, ReadSource(_source));
		_source++;
		_destination++;
		_bytesToCopy--;

		if (_destination % BlockBytes != 0) continue;

		if (--_blocksLeft == 0 || _destination == 0)
		{
			Finish();
			return;
		}
	}
}

void VramDma::Finish()
{
	_blocksLeft = 0;
	_bytesToCopy = 0;
	_hblankTransfer = false;
}

void VramDma::HBlankStarted()
{
	if (!_hblankTransfer || _bytesToCopy > 0) return;

	if (_cpu->IsHalted())
	{
		_blockHeldByHalt = true;
		return;
	}

	StartCopying(BlockBytes);
}

void VramDma::CpuWoke()
{
	// As in SameBoy, a block held back by HALT is copied on waking only if the H-Blank it was due in is still going
	if (_blockHeldByHalt && _hblankTransfer && _bytesToCopy == 0 && _graphics.InHBlankForVramDma()) StartCopying(BlockBytes);
	_blockHeldByHalt = false;
}

void VramDma::WriteState(StateWriter& writer) const
{
	writer.BeginSection("HDMA");
	writer.WriteU16(_source);
	writer.WriteU16(_destination);
	writer.WriteInt(_blocksLeft);
	writer.WriteBool(_hblankTransfer);
	writer.WriteInt(_bytesToCopy);
	writer.WriteBool(_blockHeldByHalt);
	writer.EndSection();
}

void VramDma::ReadState(StateReader& reader)
{
	reader.BeginSection("HDMA");
	_source = reader.ReadU16() & 0xfff0;
	_destination = reader.ReadU16() & 0xfff0;
	_blocksLeft = reader.ReadInt(0, MaxBlocks);
	_hblankTransfer = reader.ReadBool();
	_bytesToCopy = reader.ReadInt(0, MaxBlocks * BlockBytes);
	_blockHeldByHalt = reader.ReadBool();
	if (_bytesToCopy > _blocksLeft * BlockBytes) reader.Fail("its VRAM DMA has more bytes to copy than blocks");
	reader.EndSection();
}
