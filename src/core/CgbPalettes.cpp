#include "stdafx.h"
#include "CgbPalettes.h"
#include "StateStream.h"
#include <algorithm>

unsigned char CgbPalettes::ReadData(Set set, bool blocked) const
{
	if (blocked) return 0xff;

	auto& ram = Ram(set);
	return ram.Data[ram.Index & AddressMask];
}

void CgbPalettes::WriteData(Set set, unsigned char value, bool blocked)
{
	auto& ram = Ram(set);
	if (!blocked) ram.Data[ram.Index & AddressMask] = value;

	// The address wraps from 63 to 0
	if (ram.Index & AutoIncrement) ram.Index = AutoIncrement | ((ram.Index + 1) & AddressMask);
}

void CgbPalettes::ApplyCgbPostBootState()
{
	auto& background = Ram(Set::Background);
	std::fill(std::begin(background.Data), std::end(background.Data), 0xff);
	for (auto i = 1; i < PaletteRamBytes; i += 2) background.Data[i] = 0x7f;
}

void CgbPalettes::WriteState(StateWriter& writer) const
{
	for (auto& ram : _sets)
	{
		writer.WriteU8(ram.Index);
		writer.WriteBytes(ram.Data, sizeof(ram.Data));
	}
}

void CgbPalettes::ReadState(StateReader& reader)
{
	for (auto& ram : _sets)
	{
		ram.Index = reader.ReadU8() & (AutoIncrement | AddressMask);
		reader.ReadBytes(ram.Data, sizeof(ram.Data));
	}
}
