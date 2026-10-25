#pragma once
#include "Cartridge.h"
#include <fstream>
#include <memory>

class CartridgeFactory
{
public:
	// Number of 8 KB RAM banks for each value of the cartridge header's RAM size byte (0x149)
	static int RamBanksFromHeader(const std::vector<unsigned char>& rom)
	{
		static const int banks[] = { 0, 1, 1, 4, 16, 8 };
		return rom.size() > 0x149 && rom[0x149] < 6 ? banks[rom[0x149]] : 0;
	}

	// ramBanks < 0 takes the RAM size from the cartridge header
	static std::shared_ptr<Cartridge> LoadFromFile(std::string filePath, int ramBanks)
	{
		std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
		if (!ifs) return nullptr;

		auto pos = ifs.tellg();

		auto buffer = std::vector<unsigned char>(static_cast<int>(pos));

		ifs.seekg(0, std::ios::beg);
		ifs.read(reinterpret_cast<char*>(buffer.data()), pos);

		if (ramBanks < 0) ramBanks = RamBanksFromHeader(buffer);

		return std::make_shared<Cartridge>(std::move(buffer), ramBanks);
	}
};
