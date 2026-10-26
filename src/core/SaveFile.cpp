#include "stdafx.h"
#include "SaveFile.h"
#include "Mbc2Cartridge.h"
#include "RealTimeClock.h"
#include <algorithm>
#include <sstream>

namespace
{
	const size_t RegisterFieldSize = 4;

	// The bits each clock register holds, per https://gbdev.io/pandocs/MBC3.html
	const unsigned char RegisterMasks[RealTimeClock::RegisterCount] = { 0x3f, 0x3f, 0x1f, 0xff, 0xc1 };

	const unsigned char Mbc2UnusedBits = 0xf0;
	const size_t Mbc2PackedSize = Mbc2Cartridge::RamSize / 2;

	bool IsMbc2(Cartridge& cartridge)
	{
		return dynamic_cast<Mbc2Cartridge*>(&cartridge) != nullptr;
	}

	// mGBA's MBC2 save, with two nibbles per byte, as one byte per nibble
	std::vector<unsigned char> UnpackMbc2Nibbles(const std::vector<unsigned char>& data)
	{
		std::vector<unsigned char> unpacked;
		for (auto byte : data)
		{
			unpacked.push_back(byte & 0x0f);
			unpacked.push_back(byte >> 4);
		}

		return unpacked;
	}

	void AppendLittleEndian(std::vector<unsigned char>& out, uint64_t value, size_t bytes)
	{
		for (size_t i = 0; i < bytes; i++) out.push_back(static_cast<unsigned char>(value >> (8 * i)));
	}

	uint64_t ReadLittleEndian(const unsigned char* in, size_t bytes)
	{
		uint64_t value = 0;
		for (size_t i = 0; i < bytes; i++) value |= static_cast<uint64_t>(in[i]) << (8 * i);
		return value;
	}

	void AppendRegisters(std::vector<unsigned char>& out, const std::array<unsigned char, RealTimeClock::RegisterCount>& registers)
	{
		for (auto value : registers) AppendLittleEndian(out, value, RegisterFieldSize);
	}

	const unsigned char* ReadRegisters(const unsigned char* in, std::array<unsigned char, RealTimeClock::RegisterCount>& registers)
	{
		for (auto reg = 0; reg < RealTimeClock::RegisterCount; reg++, in += RegisterFieldSize)
		{
			registers[reg] = static_cast<unsigned char>(ReadLittleEndian(in, RegisterFieldSize)) & RegisterMasks[reg];
		}

		return in;
	}

	void RestoreClock(RealTimeClock& clock, const unsigned char* footer, size_t footerSize, int64_t unixTime)
	{
		RealTimeClock::State state;
		footer = ReadRegisters(footer, state.Counters);
		footer = ReadRegisters(footer, state.Latched);
		clock.SetState(state);

		// A clock set back since the save doesn't wind the Game Boy's clock back. A zero time stamp means the time
		// wasn't known, so moving the clock on by decades would be wrong. Gaps longer than the 512-day counter
		// set its carry bit, as they would on the cartridge
		auto savedTime = ReadLittleEndian(footer, footerSize - 2 * RealTimeClock::RegisterCount * RegisterFieldSize);
		if (savedTime != 0 && unixTime > 0 && savedTime < static_cast<uint64_t>(unixTime))
		{
			clock.AdvanceSeconds(static_cast<uint64_t>(unixTime) - savedTime);
		}
	}
}

size_t SaveFile::ExpectedSize(Cartridge& cartridge)
{
	return cartridge.GetRam().size() + (cartridge.GetRealTimeClock() ? RtcFooterSize : 0);
}

std::vector<unsigned char> SaveFile::Serialise(Cartridge& cartridge, int64_t unixTime)
{
	std::vector<unsigned char> data(cartridge.GetRam());
	data.reserve(ExpectedSize(cartridge));

	if (IsMbc2(cartridge))
	{
		for (auto& byte : data) byte |= Mbc2UnusedBits;
	}

	if (auto clock = cartridge.GetRealTimeClock())
	{
		auto& state = clock->GetState();
		AppendRegisters(data, state.Counters);
		AppendRegisters(data, state.Counters);
		AppendLittleEndian(data, static_cast<uint64_t>(unixTime), RtcFooterSize - 2 * RealTimeClock::RegisterCount * RegisterFieldSize);
	}

	return data;
}

std::string SaveFile::Deserialise(Cartridge& cartridge, const std::vector<unsigned char>& save, int64_t unixTime)
{
	auto isMbc2 = IsMbc2(cartridge);
	auto isPackedMbc2 = isMbc2 && save.size() == Mbc2PackedSize;
	std::vector<unsigned char> unpacked;
	if (isPackedMbc2) unpacked = UnpackMbc2Nibbles(save);
	const auto& data = isPackedMbc2 ? unpacked : save;

	auto& ram = cartridge.GetRam();
	auto clock = cartridge.GetRealTimeClock();

	// The footer is recognised by its size, as it has no signature
	size_t footerSize = 0;
	if (clock != nullptr)
	{
		if (data.size() == ram.size() + RtcFooterSize) footerSize = RtcFooterSize;
		else if (data.size() == ram.size() + LegacyRtcFooterSize) footerSize = LegacyRtcFooterSize;
	}

	auto ramData = data.size() - footerSize;
	auto ramToLoad = std::min(ramData, ram.size());
	std::copy(data.begin(), data.begin() + ramToLoad, ram.begin());

	if (isMbc2)
	{
		for (size_t i = 0; i < ramToLoad; i++) ram[i] &= ~Mbc2UnusedBits;
	}

	if (footerSize != 0) RestoreClock(*clock, data.data() + ramData, footerSize, unixTime);

	std::ostringstream warning;
	if (ramData != ram.size() || (clock != nullptr && footerSize == 0))
	{
		warning << "The save is " << data.size() << " bytes but " << ExpectedSize(cartridge) << " were expected";
		if (ramData < ram.size()) warning << "; the rest of the RAM is left blank";
		if (ramData > ram.size()) warning << "; the extra data is ignored";
		if (clock != nullptr && footerSize == 0) warning << "; it has no clock data, so the clock starts from zero";
	}

	// What was loaded is what is on disk
	cartridge.MarkSaved();
	return warning.str();
}
