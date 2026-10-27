#pragma once

class StateWriter;
class StateReader;

// The CGB's colour palette RAM (https://gbdev.io/pandocs/Palettes.html#lcd-color-palettes-cgb-only): 8 background
// and 8 object palettes of 4 colours, each colour 15-bit RGB stored low byte first. Each set is reached through an
// index register (BCPS/OCPS) and a data register (BCPD/OCPD)
class CgbPalettes
{
public:
	static const int PaletteRamBytes = 64;
	static const int ColoursPerPalette = 4;

	enum class Set { Background, Objects };

private:
	static const unsigned char AutoIncrement = 0x80;
	static const unsigned char AddressMask = 0x3f;

	struct PaletteRam
	{
		unsigned char Index{ 0 };
		unsigned char Data[PaletteRamBytes]{};
	};

	PaletteRam _sets[2];

	PaletteRam& Ram(Set set) { return _sets[static_cast<int>(set)]; }
	const PaletteRam& Ram(Set set) const { return _sets[static_cast<int>(set)]; }

public:
	// The index register: bit 7 auto-increment, bits 0-5 the byte addressed; bit 6 reads 1 (SameBoy)
	unsigned char ReadIndex(Set set) const { return Ram(set).Index | 0x40; }
	void WriteIndex(Set set, unsigned char value) { Ram(set).Index = value & (AutoIncrement | AddressMask); }

	// The data register. While the PPU is drawing (mode 3) it can't be accessed: reads give $FF and writes are lost,
	// though a write still advances an auto-incrementing index (Pan Docs)
	unsigned char ReadData(Set set, bool blocked) const;
	void WriteData(Set set, unsigned char value, bool blocked);

	// A colour as 15-bit RGB
	unsigned short Colour(Set set, int palette, int colour) const
	{
		auto& data = Ram(set).Data;
		auto offset = (palette * ColoursPerPalette + colour) * 2;
		return static_cast<unsigned short>((data[offset] | data[offset + 1] << 8) & 0x7fff);
	}

	// The CGB boot ROM sets every background colour to white and leaves the object colours as they powered up
	// (zeroed here for repeatability)
	void ApplyCgbPostBootState();

	void WriteState(StateWriter& writer) const;
	void ReadState(StateReader& reader);
};
