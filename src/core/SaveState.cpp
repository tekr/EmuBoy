#include "stdafx.h"
#include "SaveState.h"
#include "StateStream.h"
#include "Crc32.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace
{
	const char Magic[] = "EMUBOYSS";
	const size_t MagicSize = sizeof(Magic) - 1;
	const size_t CrcSize = sizeof(uint32_t);

	const size_t TitleOffset = 0x134;
	const size_t TitleSize = 16;

	// What identifies the game a state belongs to
	struct RomIdentity
	{
		std::array<unsigned char, TitleSize> Title{};
		uint32_t Size{ 0 };
		uint32_t Crc{ 0 };

		explicit RomIdentity(const Cartridge& cartridge)
		{
			// Cartridges pad their ROM to at least one bank, so the header is always there
			auto& rom = cartridge.GetRom();
			std::copy(rom.begin() + TitleOffset, rom.begin() + TitleOffset + TitleSize, Title.begin());
			Size = static_cast<uint32_t>(rom.size());
			Crc = cartridge.GetRomCrc();
		}

		RomIdentity() = default;

		bool operator==(const RomIdentity& other) const
		{
			return Title == other.Title && Size == other.Size && Crc == other.Crc;
		}

		void Write(StateWriter& writer) const
		{
			writer.BeginSection("ROM ");
			writer.WriteBytes(Title.data(), Title.size());
			writer.WriteU32(Size);
			writer.WriteU32(Crc);
			writer.EndSection();
		}

		void Read(StateReader& reader)
		{
			reader.BeginSection("ROM ");
			reader.ReadBytes(Title.data(), Title.size());
			Size = reader.ReadU32();
			Crc = reader.ReadU32();
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
		if (version < SaveState::OldestFormatVersion || version > SaveState::FormatVersion)
		{
			throw StateError("The save state is in version " + std::to_string(version) + " of the format; this version of "
				"Emu Boy reads versions " + std::to_string(SaveState::OldestFormatVersion) + " to " +
				std::to_string(SaveState::FormatVersion));
		}

		if (state.size() < MagicSize + sizeof(uint32_t) + CrcSize) throw StateError("The save state is truncated");
		auto bodySize = state.size() - CrcSize;

		StateReader crc{ state.data() + bodySize, CrcSize };
		if (crc.ReadU32() != Crc32::Compute(state.data(), bodySize))
		{
			throw StateError("The save state is corrupt: its checksum doesn't match");
		}

		StateReader reader{ state.data() + MagicSize + sizeof(uint32_t), bodySize - MagicSize - sizeof(uint32_t) };
		reader.SetFormatVersion(version);

		RomIdentity saved;
		saved.Read(reader);
		RomIdentity loaded{ emulator.GetCartridge() };
		if (!(saved == loaded))
		{
			throw StateError("The save state is for another game (" + (saved.TitleText().empty() ? "untitled" : saved.TitleText()) + ")");
		}

		emulator.ReadState(reader);
		if (!reader.AtEnd()) reader.Fail("it holds more than was read");
	}
}

const uint32_t SaveState::FormatVersion;
const uint32_t SaveState::OldestFormatVersion;

std::vector<unsigned char> SaveState::Create(const Emulator& emulator)
{
	StateWriter writer;
	writer.WriteBytes(reinterpret_cast<const unsigned char*>(Magic), MagicSize);
	writer.WriteU32(FormatVersion);
	RomIdentity{ emulator.GetCartridge() }.Write(writer);
	emulator.WriteState(writer);

	auto& data = writer.GetData();
	writer.WriteU32(Crc32::Compute(data.data(), data.size()));
	return writer.TakeData();
}

std::string SaveState::Restore(Emulator& emulator, const std::vector<unsigned char>& state)
{
	try
	{
		// Worked out before cloning, so the clone shares the result rather than working it out again
		emulator.GetCartridge().GetRomCrc();

		auto trial = std::make_unique<Emulator>(emulator.GetCartridge().Clone());
		Read(*trial, state);
	}
	catch (const StateError& e)
	{
		return e.what();
	}
	catch (const std::bad_alloc&)
	{
		return "There isn't enough memory to load the save state";
	}

	// Reading depends only on the state and the cartridge, so having loaded into the copy, it loads here too
	Read(emulator, state);
	return "";
}
