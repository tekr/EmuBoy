#pragma once
#include "Cartridge.h"
#include <array>
#include <fstream>
#include <memory>

class CartridgeFactory
{
public:
	static const size_t RamSizeHeaderOffset = 0x149;

	static int RamBanksFromHeader(const std::vector<unsigned char>& rom)
	{
		// 8 KB banks per RAM size code. Code 1 (2 KB) still needs a whole bank, and code 5 (64 KB) is smaller than code 4 (128 KB)
		static const std::array<int, 6> ramBanksBySizeCode{ { 0, 1, 1, 4, 16, 8 } };

		if (rom.size() <= RamSizeHeaderOffset) return 0;

		auto sizeCode = rom[RamSizeHeaderOffset];
		return sizeCode < ramBanksBySizeCode.size() ? ramBanksBySizeCode[sizeCode] : 0;
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
