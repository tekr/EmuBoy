#pragma once
#include <vector>
#include <cstring>
#include "../core/Mbc1Cartridge.h"
#include "../core/MemoryMap.h"

class TestCartridge : public Mbc1Cartridge
{
public:
	TestCartridge() : Mbc1Cartridge(std::vector<unsigned char>(MemoryMap::RomBankSize), Cartridge::RamBankSize)
	{
	}

	void SetBytes(unsigned short address, std::vector<unsigned char>&& bytes)
	{
		memcpy(_rom.data() + address, bytes.data(), bytes.size());
	}
};

class TestMemoryMap : public MemoryMap
{
	std::shared_ptr<TestCartridge> _testCartridge;

public:
	const int TestRamSize = 0x20;

	explicit TestMemoryMap(InputJoypad& joypad) : MemoryMap(joypad), _testCartridge{ std::make_shared<TestCartridge>() }
	{
		SetCartridge(_testCartridge);
		SetInternalRomEnabled(false);
	}

	unsigned char& operator[](unsigned short address);
	bool operator==(const TestMemoryMap& other) const;
	void operator=(const TestMemoryMap& other);

	void SetBytes(unsigned short address, std::vector<unsigned char>&& bytes);

	void SetInternalRomEnabled(bool enabled);
};

