#pragma once
#include <vector>
#include "../core/Cartridge.h"
#include "../core/MemoryMap.h"

class TestCartridge : public Cartridge
{
public:
	TestCartridge::TestCartridge() : Cartridge(std::vector<unsigned char>(MemoryMap::RomBankSize), 1)
	{
	}

	void SetBytes(unsigned short address, std::vector<unsigned char>&& bytes)
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

	unsigned char& operator[](unsigned short address);
	bool operator==(const TestMemoryMap& other) const;
	void operator=(const TestMemoryMap& other);

	void SetBytes(unsigned short address, std::vector<unsigned char>&& bytes);

	void SetInternalRomEnabled(bool enabled);
};

