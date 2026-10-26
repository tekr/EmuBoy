#include "stdafx.h"
#include "OamDma.h"
#include "MemoryMap.h"
#include "Graphics.h"

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
