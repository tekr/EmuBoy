#pragma once
#include "Cartridge.h"
#include <fstream>
#include <memory>

class CartridgeFactory
{
public:
	static std::unique_ptr<Cartridge> LoadFromFile(std::string filePath, int ramBanks)
	{
		std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
		if (!ifs) return nullptr;

		auto pos = ifs.tellg();

		auto buffer = std::vector<uint8_t>(static_cast<int>(pos));

		ifs.seekg(0, std::ios::beg);
		ifs.read(reinterpret_cast<char*>(buffer.data()), pos);

		return std::make_unique<Cartridge>(std::move(buffer), ramBanks);
	}
};
