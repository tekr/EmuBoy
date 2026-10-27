#include "stdafx.h"
#include "CartridgeFactory.h"
#include "RomOnlyCartridge.h"
#include "Mbc1Cartridge.h"
#include "Mbc2Cartridge.h"
#include "Mbc3Cartridge.h"
#include "Mbc5Cartridge.h"
#include <algorithm>
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

	// Mappers that aren't emulated run as MBC1, but keep their battery so the game's save isn't lost: MMM01+RAM+BATTERY,
	// MBC7+SENSOR+RUMBLE+RAM+BATTERY, Pocket Camera, HuC3 (which also has a clock) and HuC1+RAM+BATTERY
	case 0x0d: return { MapperType::Mbc1, true, false, false, false };
	case 0x22: return { MapperType::Mbc1, true, false, true, false };
	case 0xfc: return { MapperType::Mbc1, true, false, false, false };
	case 0xfe: return { MapperType::Mbc1, true, true, false, false };
	case 0xff: return { MapperType::Mbc1, true, false, false, false };
	default: return { MapperType::Mbc1, false, false, false, false };
	}
}

size_t CartridgeFactory::RamSizeFromHeader(const std::vector<unsigned char>& rom)
{
	// Per https://gbdev.io/pandocs/The_Cartridge_Header.html#0149--ram-size. No licensed cartridge uses code 1, but
	// it is a 2 KB chip (mirrored through $A000-$BFFF) in BGB, SameBoy and VBA-M, whose save files are 2 KB. Code 5
	// (64 KB) is smaller than code 4 (128 KB)
	static const std::array<size_t, 6> ramSizeBySizeCode{ { 0, 2 << 10, 8 << 10, 32 << 10, 128 << 10, 64 << 10 } };

	if (rom.size() <= RamSizeHeaderOffset) return 0;

	auto sizeCode = rom[RamSizeHeaderOffset];
	return sizeCode < ramSizeBySizeCode.size() ? ramSizeBySizeCode[sizeCode] : 0;
}

std::string CartridgeFactory::TitleFromHeader(const std::vector<unsigned char>& rom)
{
	const size_t titleOffset = 0x134;
	const size_t manufacturerCodeOffset = 0x13f;
	const size_t colourFlagOffset = 0x143;
	auto titleEnd = std::min<size_t>(0x144, rom.size());

	// Colour-era cartridges may shorten the title to 11 characters, followed by a 4-character manufacturer code of
	// capital letters and digits. The header doesn't say which, so a code is assumed only when one could be there
	if (rom.size() > colourFlagOffset && (rom[colourFlagOffset] & 0x80) != 0 &&
		std::all_of(rom.begin() + manufacturerCodeOffset, rom.begin() + colourFlagOffset,
			[](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }))
	{
		titleEnd = manufacturerCodeOffset;
	}

	std::string title;
	for (auto i = titleOffset; i < titleEnd; i++)
	{
		auto c = rom[i];
		if (c < 0x20 || c > 0x7e) break;
		title += static_cast<char>(c);
	}

	auto first = title.find_first_not_of(' ');
	if (first == std::string::npos) return "";
	return title.substr(first, title.find_last_not_of(' ') - first + 1);
}

std::shared_ptr<Cartridge> CartridgeFactory::Create(std::vector<unsigned char>&& rom)
{
	auto ramSize = RamSizeFromHeader(rom);
	return Create(std::move(rom), ramSize);
}

std::shared_ptr<Cartridge> CartridgeFactory::Create(std::vector<unsigned char>&& rom, size_t ramSize)
{
	auto type = TypeFromHeader(rom);

	switch (type.Mapper)
	{
	case MapperType::RomOnly:
		return std::make_shared<RomOnlyCartridge>(std::move(rom), ramSize, type.HasBattery);

	case MapperType::Mbc2:
		// MBC2's RAM is built in, so the header declares none
		return std::make_shared<Mbc2Cartridge>(std::move(rom), type.HasBattery);

	case MapperType::Mbc3:
	{
		auto isMbc30 = Mbc3Cartridge::IsMbc30(rom.size(), ramSize);
		return std::make_shared<Mbc3Cartridge>(std::move(rom), ramSize, type.HasBattery, type.HasClock, isMbc30);
	}

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

std::shared_ptr<Cartridge> CartridgeFactory::LoadFromFile(const std::string& filePath, size_t ramSize)
{
	std::vector<unsigned char> rom;
	return ReadFile(filePath, rom) ? Create(std::move(rom), ramSize) : nullptr;
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
