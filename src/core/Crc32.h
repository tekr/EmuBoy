#pragma once
#include <cstddef>
#include <cstdint>

// CRC-32 (ISO-HDLC, as zlib and PNG use)
class Crc32
{
public:
	static uint32_t Compute(const unsigned char* data, size_t size);
};
