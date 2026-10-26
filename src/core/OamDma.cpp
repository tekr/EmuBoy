#include "stdafx.h"
#include "OamDma.h"
#include "MemoryMap.h"
#include "Graphics.h"
#include "StateStream.h"

void OamDma::WriteRegister(unsigned char value)
{
	_register = value;
	_pendingSource = static_cast<unsigned short>(value << 8);
	_startDelay = StartDelayCycles;
}

void OamDma::Step()
{
	if (_startDelay > 0 && --_startDelay == 0)
	{
		_source = _pendingSource;
		_bytesTransferred = 0;
		_transferring = true;
	}

	if (!_transferring) return;

	if (_bytesTransferred == TransferBytes)
	{
		_transferring = false;
		return;
	}

	// On the DMG, sources from $E000 up read work RAM through its echo
	auto address = static_cast<unsigned short>(_source + _bytesTransferred);
	if (address >= 0xe000) address -= 0x2000;

	_busValue = _memoryMap.ReadByteForDma(address);
	_graphics.WriteOamDirect(static_cast<unsigned short>(_bytesTransferred++), _busValue);
}

void OamDma::WriteState(StateWriter& writer) const
{
	writer.BeginSection("DMA ");
	writer.WriteU8(_register);
	writer.WriteInt(_startDelay);
	writer.WriteU16(_pendingSource);
	writer.WriteBool(_transferring);
	writer.WriteU16(_source);
	writer.WriteInt(_bytesTransferred);
	writer.WriteU8(_busValue);
	writer.EndSection();
}

void OamDma::ReadState(StateReader& reader)
{
	reader.BeginSection("DMA ");
	_register = reader.ReadU8();
	_startDelay = reader.ReadInt(0, StartDelayCycles);
	_pendingSource = reader.ReadU16();
	_transferring = reader.ReadBool();
	_source = reader.ReadU16();
	_bytesTransferred = reader.ReadInt(0, TransferBytes);
	_busValue = reader.ReadU8();
	reader.EndSection();
}
