#include "stdafx.h"
#include "StateStream.h"
#include <cmath>
#include <cstring>
#include <limits>

static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == sizeof(uint64_t),
	"Doubles are stored as their IEEE 754 bit pattern");

namespace
{
	const size_t TagSize = 4;
}

void StateWriter::WriteU16(uint16_t value)
{
	WriteU8(static_cast<uint8_t>(value));
	WriteU8(static_cast<uint8_t>(value >> 8));
}

void StateWriter::WriteU32(uint32_t value)
{
	WriteU16(static_cast<uint16_t>(value));
	WriteU16(static_cast<uint16_t>(value >> 16));
}

void StateWriter::WriteU64(uint64_t value)
{
	WriteU32(static_cast<uint32_t>(value));
	WriteU32(static_cast<uint32_t>(value >> 32));
}

void StateWriter::WriteDouble(double value)
{
	uint64_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	WriteU64(bits);
}

void StateWriter::WriteBytes(const unsigned char* bytes, size_t count)
{
	_data.insert(_data.end(), bytes, bytes + count);
}

void StateWriter::WriteTag(const char* tag)
{
	if (std::strlen(tag) != TagSize) throw std::logic_error("Save state tags are four characters");
	WriteBytes(reinterpret_cast<const unsigned char*>(tag), TagSize);
}

void StateWriter::BeginSection(const char* tag)
{
	WriteTag(tag);
	_openSections.push_back(_data.size());
	WriteU32(0);
}

void StateWriter::EndSection()
{
	if (_openSections.empty()) throw std::logic_error("Save state section ended without being begun");

	auto lengthPosition = _openSections.back();
	_openSections.pop_back();

	auto length = static_cast<uint32_t>(_data.size() - lengthPosition - sizeof(uint32_t));
	for (size_t i = 0; i < sizeof(uint32_t); i++) _data[lengthPosition + i] = static_cast<unsigned char>(length >> (8 * i));
}

void StateReader::Fail(const std::string& problem) const
{
	auto where = _openSections.empty() ? std::string{} : " in its " + _openSections.back().Tag + " section";
	throw StateError("The save state is invalid" + where + ": " + problem);
}

const unsigned char* StateReader::Take(size_t count)
{
	if (count > Limit() - _position) Fail(Limit() == _size ? "it is truncated" : "a value runs past the section's end");

	auto bytes = _data + _position;
	_position += count;
	return bytes;
}

uint16_t StateReader::ReadU16()
{
	auto bytes = Take(2);
	return static_cast<uint16_t>(bytes[0] | bytes[1] << 8);
}

uint32_t StateReader::ReadU32()
{
	uint32_t low = ReadU16();
	return low | static_cast<uint32_t>(ReadU16()) << 16;
}

uint64_t StateReader::ReadU64()
{
	uint64_t low = ReadU32();
	return low | static_cast<uint64_t>(ReadU32()) << 32;
}

int32_t StateReader::ReadInt(int32_t min, int32_t max)
{
	auto value = static_cast<int32_t>(ReadU32());
	if (value < min || value > max)
	{
		Fail("a value is " + std::to_string(value) + ", outside " + std::to_string(min) + " to " + std::to_string(max));
	}

	return value;
}

bool StateReader::ReadBool()
{
	auto value = ReadU8();
	if (value > 1) Fail("a flag is " + std::to_string(value) + " rather than 0 or 1");
	return value == 1;
}

double StateReader::ReadDouble()
{
	auto bits = ReadU64();
	double value;
	std::memcpy(&value, &bits, sizeof(value));
	if (!std::isfinite(value)) Fail("a number is infinite or not a number");
	return value;
}

void StateReader::ReadBytes(unsigned char* bytes, size_t count)
{
	std::memcpy(bytes, Take(count), count);
}

void StateReader::ExpectTag(const char* tag, const std::string& what)
{
	auto bytes = Take(TagSize);
	if (std::memcmp(bytes, tag, TagSize) != 0) Fail("expected " + what);
}

void StateReader::BeginSection(const char* tag)
{
	std::string name{ tag };
	name.erase(name.find_last_not_of(' ') + 1);
	ExpectTag(tag, "the " + name + " section");

	auto length = ReadU32();
	if (length > Limit() - _position) Fail("its " + name + " section runs past the end");

	_openSections.push_back({ name, _position + length });
}

void StateReader::EndSection()
{
	if (_openSections.empty()) throw std::logic_error("Save state section ended without being begun");
	if (_position != _openSections.back().End) Fail("it holds more than was read");

	_openSections.pop_back();
}
