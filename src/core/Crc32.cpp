#include "stdafx.h"
#include "Crc32.h"
#include <array>

namespace
{
	std::array<uint32_t, 256> MakeTable()
	{
		std::array<uint32_t, 256> table{};
		for (uint32_t i = 0; i < table.size(); i++)
		{
			auto crc = i;
			for (auto bit = 0; bit < 8; bit++) crc = crc & 1 ? 0xedb88320 ^ crc >> 1 : crc >> 1;
			table[i] = crc;
		}

		return table;
	}
}

uint32_t Crc32::Compute(const unsigned char* data, size_t size)
{
	static const auto table = MakeTable();

	uint32_t crc = 0xffffffff;
	for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xff] ^ crc >> 8;
	return ~crc;
}
