#include "stdafx.h"
#include "CartridgeFactory.h"
#include "RomOnlyCartridge.h"
#include "Mbc1Cartridge.h"
#include "Mbc2Cartridge.h"
#include "Mbc3Cartridge.h"
#include "Mbc5Cartridge.h"
#include <array>
#include <fstream>

CartridgeFactory::CartridgeType CartridgeFactory::TypeFromHeader(const std::vector<unsigned char>& rom)
{
	auto typeCode = rom.size() > CartridgeTypeHeaderOffset ? rom[CartridgeTypeHeaderOffset] : 0;

	// Per https://gbdev.io/pandocs/The_Cartridge_Header.html#0147--cartridge-type
	switch (typeCode)
	{
	case 0x00: case 0x08: return { MapperType::RomOnly, false, false, false, true };
	case 0x09: return { MapperType::RomOnly, true, false, false, true };
	case 0x01: case 0x02: return { MapperType::Mbc1, false, false, false, true };
	case 0x03: return { MapperType::Mbc1, true, false, false, true };
	case 0x05: return { MapperType::Mbc2, false, false, false, true };
	case 0x06: return { MapperType::Mbc2, true, false, false, true };
	case 0x0f: case 0x10: return { MapperType::Mbc3, true, true, false, true };
	case 0x11: case 0x12: return { MapperType::Mbc3, false, false, false, true };
	case 0x13: return { MapperType::Mbc3, true, false, false, true };
	case 0x19: case 0x1a: return { MapperType::Mbc5, false, false, false, true };
	case 0x1b: return { MapperType::Mbc5, true, false, false, true };
	case 0x1c: case 0x1d: return { MapperType::Mbc5, false, false, true, true };
	case 0x1e: return { MapperType::Mbc5, true, false, true, true };
	default: return { MapperType::Mbc1, false, false, false, false };
	}
}

int CartridgeFactory::RamBanksFromHeader(const std::vector<unsigned char>& rom)
{
	// 8 KB banks per RAM size code. Code 1 (2 KB) still needs a whole bank, and code 5 (64 KB) is smaller than code 4 (128 KB)
	static const std::array<int, 6> ramBanksBySizeCode{ { 0, 1, 1, 4, 16, 8 } };

	if (rom.size() <= RamSizeHeaderOffset) return 0;

	auto sizeCode = rom[RamSizeHeaderOffset];
	return sizeCode < ramBanksBySizeCode.size() ? ramBanksBySizeCode[sizeCode] : 0;
}

std::shared_ptr<Cartridge> CartridgeFactory::Create(std::vector<unsigned char>&& rom)
{
	auto ramBanks = RamBanksFromHeader(rom);
	return Create(std::move(rom), ramBanks);
}

std::shared_ptr<Cartridge> CartridgeFactory::Create(std::vector<unsigned char>&& rom, int ramBanks)
{
	auto type = TypeFromHeader(rom);
	auto ramSize = ramBanks * Cartridge::RamBankSize;

	switch (type.Mapper)
	{
	case MapperType::RomOnly:
		return std::make_shared<RomOnlyCartridge>(std::move(rom), ramSize, type.HasBattery);

	case MapperType::Mbc2:
		// MBC2's RAM is built in, so the header declares none
		return std::make_shared<Mbc2Cartridge>(std::move(rom), type.HasBattery);

	case MapperType::Mbc3:
		return std::make_shared<Mbc3Cartridge>(std::move(rom), ramSize, type.HasBattery, type.HasClock);

	case MapperType::Mbc5:
		return std::make_shared<Mbc5Cartridge>(std::move(rom), ramSize, type.HasBattery, type.HasRumble);

	default:
	{
		auto isMulticart = Mbc1Cartridge::IsMulticart(rom);
		return std::make_shared<Mbc1Cartridge>(std::move(rom), ramSize, type.HasBattery, isMulticart);
	}
	}
}

std::shared_ptr<Cartridge> CartridgeFactory::LoadFromFile(const std::string& filePath)
{
	std::vector<unsigned char> rom;
	return ReadFile(filePath, rom) ? Create(std::move(rom)) : nullptr;
}

std::shared_ptr<Cartridge> CartridgeFactory::LoadFromFile(const std::string& filePath, int ramBanks)
{
	std::vector<unsigned char> rom;
	return ReadFile(filePath, rom) ? Create(std::move(rom), ramBanks) : nullptr;
}

bool CartridgeFactory::ReadFile(const std::string& filePath, std::vector<unsigned char>& contents)
{
	std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
	if (!ifs) return false;

	auto size = ifs.tellg();
	if (size < 0) return false;

	contents = std::vector<unsigned char>(static_cast<size_t>(size));

	ifs.seekg(0, std::ios::beg);
	return static_cast<bool>(ifs.read(reinterpret_cast<char*>(contents.data()), size));
}
