#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// The save state format's current version (see SaveState). Version 2 lacks the Game Boy Color's state; version 3
// adds it at the end of each component's section, and a section saying which model the state is for
const uint32_t StateFormatVersion = 3;

// Thrown when a save state can't be read: it is truncated or corrupt, or doesn't match the emulator
class StateError : public std::runtime_error
{
public:
	explicit StateError(const std::string& message) : std::runtime_error(message) {}
};

// Writes save state data as fixed-width little-endian values, so the format depends on neither the compiler's
// structure layout nor the machine's byte order. Each component writes its state as a section: a four-character
// tag and the section's length, then the values, so that reading can check each component read exactly what it wrote
class StateWriter
{
	std::vector<unsigned char> _data;

	// Where each open section's length goes, filled in when it ends
	std::vector<size_t> _openSections;

public:
	// Tags are four characters, e.g. "CPU "
	void BeginSection(const char* tag);
	void EndSection();

	void WriteU8(uint8_t value) { _data.push_back(value); }
	void WriteU16(uint16_t value);
	void WriteU32(uint32_t value);
	void WriteU64(uint64_t value);
	void WriteInt(int32_t value) { WriteU32(static_cast<uint32_t>(value)); }
	void WriteBool(bool value) { WriteU8(value ? 1 : 0); }
	void WriteDouble(double value);
	void WriteBytes(const unsigned char* bytes, size_t count);
	void WriteTag(const char* tag);

	const std::vector<unsigned char>& GetData() const { return _data; }

	// Moves the data out, leaving the writer empty
	std::vector<unsigned char> TakeData() { return std::move(_data); }
};

// Reads what a StateWriter wrote, checking as it goes. Every read throws StateError if it would run past the end of
// the data or of the current section, or if a value is out of the range the caller allows, so a truncated or
// corrupt state can't put a component into a state it could never reach
class StateReader
{
	const unsigned char* _data;
	size_t _size;
	size_t _position{ 0 };

	struct Section
	{
		std::string Tag;
		size_t End;
	};

	std::vector<Section> _openSections;
	uint32_t _formatVersion{ StateFormatVersion };

	// The end of the innermost open section, or of the data
	size_t Limit() const { return _openSections.empty() ? _size : _openSections.back().End; }
	const unsigned char* Take(size_t count);

public:
	StateReader(const unsigned char* data, size_t size) : _data(data), _size(size) {}
	explicit StateReader(const std::vector<unsigned char>& data) : StateReader(data.data(), data.size()) {}

	// The version of the format being read, so components can read older states
	uint32_t FormatVersion() const { return _formatVersion; }
	void SetFormatVersion(uint32_t version) { _formatVersion = version; }

	// Reads a section's header, which must have this tag
	void BeginSection(const char* tag);

	// Checks the section has been read to its end
	void EndSection();

	uint8_t ReadU8() { return *Take(1); }
	uint16_t ReadU16();
	uint32_t ReadU32();
	uint64_t ReadU64();
	int32_t ReadInt(int32_t min, int32_t max);
	bool ReadBool();
	double ReadDouble();
	void ReadBytes(unsigned char* bytes, size_t count);
	void ExpectTag(const char* tag, const std::string& what);

	// A value of an enumeration whose values run from 0 to last
	template <typename Enum>
	Enum ReadEnum(Enum last) { return static_cast<Enum>(ReadInt(0, static_cast<int32_t>(last))); }

	// Throws StateError for a problem the caller found with what it read
	[[noreturn]] void Fail(const std::string& problem) const;

	size_t Position() const { return _position; }
	bool AtEnd() const { return _position == _size; }
};
