#pragma once
#include <vector>

class Cartridge
{
protected:
	std::vector<uint8_t> _rom;
	std::vector<uint8_t> _ram;

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
	Cartridge(std::vector<uint8_t>&& rom, int ramBanks);

	uint8_t RomReadByte(uint16_t address) const;
	uint8_t RamReadByte(uint16_t address) const;

	void RomWriteByte(uint16_t address, uint8_t value);
	void RamWriteByte(uint16_t address, uint8_t value);
};
