#include "stdafx.h"
#include "TestMemoryMap.h"

unsigned char& TestMemoryMap::operator[](unsigned short address)
{
	if (address >= RamFixed && address < RamFixed + TestRamSize)
	{
		return _fixedRam[address - RamFixed];
	}

	throw std::string{ "Memory address outside test range" };
}

bool TestMemoryMap::operator==(const TestMemoryMap& other) const
{
	return memcmp(_fixedRam, other._fixedRam, TestRamSize) == 0;
}

void TestMemoryMap::operator=(const TestMemoryMap& other)
{
	memcpy(_fixedRam, other._fixedRam, TestRamSize);
}

void TestMemoryMap::SetBytes(unsigned short address, std::vector<unsigned char>&& bytes)
{
	if (address < RamVideo)
	{
		_testCartridge->SetBytes(address, std::move(bytes));
	}
	else
	{
		for (auto byte : bytes)
		{
			WriteByte(address++, byte);
		}
	}
}

void TestMemoryMap::SetInternalRomEnabled(bool enabled)
{
	_internalRomEnabled = enabled;
}
