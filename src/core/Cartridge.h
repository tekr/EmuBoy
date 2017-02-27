#pragma once
#include <vector>

class Cartridge
{
protected:
	std::vector<unsigned char> _rom;
	std::vector<unsigned char> _ram;

	int _selectedRomBank{ 1 };
	int _selectedRamBank{ 0 };

	enum class Mode
	{
		SixteenMbRom,
		FourMbRom
	};

	Mode _mode{ Mode::SixteenMbRom };
	bool _ramEnabled;

public:
	Cartridge(std::vector<unsigned char>&& rom, int ramBanks);

	unsigned char RomReadByte(unsigned short address) const;
	unsigned char RamReadByte(unsigned short address) const;

	void RomWriteByte(unsigned short address, unsigned char value);
	void RamWriteByte(unsigned short address, unsigned char value);
};
