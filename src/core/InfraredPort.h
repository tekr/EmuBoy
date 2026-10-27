#pragma once

// RP ($FF56), the CGB's infrared port (https://gbdev.io/pandocs/CGB_Registers.html#ff56--rp-cgb-mode-only-infrared-communications-port).
// Bit 0 turns the LED on, bits 6-7 enable reading, and bit 1 reads 0 while light is received. Nothing ever shines
// into Emu Boy's port, so bit 1 always reads 1 ("normal"). Bits 2, 3 and 5 read 1, and bit 4 reads 0 as on CGB
// revision E, the revision Emu Boy emulates (SameBoy's memory.c sets it only on earlier revisions), so it reads $2E at
// power-on rather than the $3E Pan Docs gives
class InfraredPort
{
	static const unsigned char WritableBits = 0xc1;
	static const unsigned char FixedBits = 0x2e;

	unsigned char _register{ 0 };

public:
	unsigned char Read() const { return (_register & WritableBits) | FixedBits; }
	void Write(unsigned char value) { _register = value & WritableBits; }

	unsigned char Value() const { return _register; }
};
