#include "stdafx.h"
#include "SaveState.h"
#include "StateStream.h"
#include <array>
#include <cstring>
#include <memory>

namespace
{
	const char Magic[] = "EMUBOYSS";
	const size_t MagicSize = sizeof(Magic) - 1;
	const size_t CrcSize = sizeof(uint32_t);

	const size_t TitleOffset = 0x134;
	const size_t TitleSize = 16;
	const size_t HeaderChecksumOffset = 0x14d;
	const size_t GlobalChecksumOffset = 0x14e;

	// What identifies the game a state belongs to
	struct RomIdentity
	{
		std::array<unsigned char, TitleSize> Title{};
		unsigned char HeaderChecksum{ 0 };
		uint16_t GlobalChecksum{ 0 };
		uint32_t Size{ 0 };

		explicit RomIdentity(const std::vector<unsigned char>& rom)
		{
			// Cartridges pad their ROM to at least one bank, so the header is always there
			std::copy(rom.begin() + TitleOffset, rom.begin() + TitleOffset + TitleSize, Title.begin());
			HeaderChecksum = rom[HeaderChecksumOffset];
			GlobalChecksum = static_cast<uint16_t>(rom[GlobalChecksumOffset] << 8 | rom[GlobalChecksumOffset + 1]);
			Size = static_cast<uint32_t>(rom.size());
		}

		RomIdentity() = default;

		bool operator==(const RomIdentity& other) const
		{
			return Title == other.Title && HeaderChecksum == other.HeaderChecksum &&
				GlobalChecksum == other.GlobalChecksum && Size == other.Size;
		}

		void Write(StateWriter& writer) const
		{
			writer.BeginSection("ROM ");
			writer.WriteBytes(Title.data(), Title.size());
			writer.WriteU8(HeaderChecksum);
			writer.WriteU16(GlobalChecksum);
			writer.WriteU32(Size);
			writer.EndSection();
		}

		void Read(StateReader& reader)
		{
			reader.BeginSection("ROM ");
			reader.ReadBytes(Title.data(), Title.size());
			HeaderChecksum = reader.ReadU8();
			GlobalChecksum = reader.ReadU16();
			Size = reader.ReadU32();
			reader.EndSection();
		}

		// The title as far as it's printable, for messages
		std::string TitleText() const
		{
			std::string text;
			for (auto c : Title)
			{
				if (c < 0x20 || c >= 0x7f) break;
				text += static_cast<char>(c);
			}

			return text;
		}
	};

	std::array<uint32_t, 256> MakeCrcTable()
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

	// Throws StateError if the state can't be read, having possibly read part of it into the emulator
	void Read(Emulator& emulator, const std::vector<unsigned char>& state)
	{
		if (state.size() < MagicSize || std::memcmp(state.data(), Magic, MagicSize) != 0)
		{
			throw StateError("The file isn't an Emu Boy save state");
		}

		// The version comes before the CRC check, so a state from another version is reported as one
		StateReader header{ state.data() + MagicSize, state.size() - MagicSize };
		auto version = header.ReadU32();
		if (version != SaveState::FormatVersion)
		{
			throw StateError("The save state is in version " + std::to_string(version) + " of the format; this version of "
				"Emu Boy reads version " + std::to_string(SaveState::FormatVersion));
		}

		if (state.size() < MagicSize + sizeof(uint32_t) + CrcSize) throw StateError("The save state is truncated");
		auto bodySize = state.size() - CrcSize;

		StateReader crc{ state.data() + bodySize, CrcSize };
		if (crc.ReadU32() != SaveState::Crc32(state.data(), bodySize))
		{
			throw StateError("The save state is corrupt: its checksum doesn't match");
		}

		StateReader reader{ state.data() + MagicSize + sizeof(uint32_t), bodySize - MagicSize - sizeof(uint32_t) };

		RomIdentity saved;
		saved.Read(reader);
		RomIdentity loaded{ emulator.GetCartridge().GetRom() };
		if (!(saved == loaded))
		{
			throw StateError("The save state is for another game (" + (saved.TitleText().empty() ? "untitled" : saved.TitleText()) + ")");
		}

		emulator.ReadState(reader);
		if (!reader.AtEnd()) reader.Fail("it holds more than was read");
	}
}

const uint32_t SaveState::FormatVersion;

uint32_t SaveState::Crc32(const unsigned char* data, size_t size)
{
	static const auto table = MakeCrcTable();

	uint32_t crc = 0xffffffff;
	for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xff] ^ crc >> 8;
	return ~crc;
}

std::vector<unsigned char> SaveState::Create(const Emulator& emulator)
{
	StateWriter writer;
	writer.WriteBytes(reinterpret_cast<const unsigned char*>(Magic), MagicSize);
	writer.WriteU32(FormatVersion);
	RomIdentity{ emulator.GetCartridge().GetRom() }.Write(writer);
	emulator.WriteState(writer);

	auto& data = writer.GetData();
	writer.WriteU32(Crc32(data.data(), data.size()));
	return data;
}

std::string SaveState::Restore(Emulator& emulator, const std::vector<unsigned char>& state)
{
	try
	{
		auto trial = std::make_unique<Emulator>(emulator.GetCartridge().Clone());
		Read(*trial, state);
	}
	catch (const StateError& e)
	{
		return e.what();
	}

	// Reading depends only on the state and the cartridge, so having loaded into the copy, it loads here too
	Read(emulator, state);
	return "";
}
