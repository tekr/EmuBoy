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

	static std::shared_ptr<Cartridge> LoadFromFile(std::string filePath)
	{
		std::vector<unsigned char> rom;
		if (!ReadFile(filePath, rom)) return nullptr;

		auto ramBanks = RamBanksFromHeader(rom);
		return std::make_shared<Cartridge>(std::move(rom), ramBanks);
	}

	static std::shared_ptr<Cartridge> LoadFromFile(std::string filePath, int ramBanks)
	{
		std::vector<unsigned char> rom;
		if (!ReadFile(filePath, rom)) return nullptr;

		return std::make_shared<Cartridge>(std::move(rom), ramBanks);
	}

private:
	static bool ReadFile(const std::string& filePath, std::vector<unsigned char>& contents)
	{
		std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
		if (!ifs) return false;

		auto pos = ifs.tellg();

		contents = std::vector<unsigned char>(static_cast<int>(pos));

		ifs.seekg(0, std::ios::beg);
		ifs.read(reinterpret_cast<char*>(contents.data()), pos);
		return true;
	}
};
