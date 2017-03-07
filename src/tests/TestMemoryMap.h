#pragma once
#include <vector>
#include "../core/Cartridge.h"
#include "../core/MemoryMap.h"

class TestCartridge : public Cartridge
{
public:
	TestCartridge::TestCartridge() : Cartridge(std::vector<uint8_t>(MemoryMap::RomBankSize), 1)
	{
	}

	void SetBytes(uint16_t address, std::vector<uint8_t>&& bytes)
	{
		memcpy(_rom.data() + address, bytes.data(), bytes.size());
	}
};

class TestMemoryMap : public MemoryMap
{
	TestCartridge _testCartridge;

public:
	const int TestRamSize = 0x20;

	explicit TestMemoryMap(InputJoypad& joypad) : MemoryMap(joypad), _testCartridge{}
	{
		SetCartridge(&_testCartridge);
		SetInternalRomEnabled(false);
	}

	uint8_t& operator[](uint16_t address);
	bool operator==(const TestMemoryMap& other) const;
	void operator=(const TestMemoryMap& other);

	void SetBytes(uint16_t address, std::vector<uint8_t>&& bytes);

	void SetInternalRomEnabled(bool enabled);
};

