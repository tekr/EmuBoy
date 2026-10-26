#pragma once
#include "Cartridge.h"
#include <memory>
#include <string>

// Creates cartridges with the mapper named by the ROM header's cartridge type
class CartridgeFactory
{
public:
	static const size_t CartridgeTypeHeaderOffset = 0x147;
	static const size_t RamSizeHeaderOffset = 0x149;

	enum class MapperType
	{
		RomOnly,
		Mbc1,
		Mbc2,
		Mbc3,
		Mbc5
	};

	struct CartridgeType
	{
		MapperType Mapper;
		bool HasBattery;
		bool HasClock;
		bool HasRumble;

		// False for mappers not emulated, which fall back to MBC1 as the most common
		bool IsSupported;
	};

	static CartridgeType TypeFromHeader(const std::vector<unsigned char>& rom);
	static int RamBanksFromHeader(const std::vector<unsigned char>& rom);

	// The game's title from $0134-$0143, up to the first byte that isn't printable ASCII (the padding $00s, or on
	// later cartridges the colour flag at $0143), with surrounding spaces trimmed. Empty if the ROM is too short
	static std::string TitleFromHeader(const std::vector<unsigned char>& rom);

	static std::shared_ptr<Cartridge> Create(std::vector<unsigned char>&& rom);

	// Overrides the header's RAM size, for ROMs that need RAM without declaring it
	static std::shared_ptr<Cartridge> Create(std::vector<unsigned char>&& rom, int ramBanks);

	// Returns null if the file can't be read
	static std::shared_ptr<Cartridge> LoadFromFile(const std::string& filePath);
	static std::shared_ptr<Cartridge> LoadFromFile(const std::string& filePath, int ramBanks);

	static bool ReadFile(const std::string& filePath, std::vector<unsigned char>& contents);
};
