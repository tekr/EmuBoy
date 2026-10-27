#include "stdafx.h"
#include "GameBoyModel.h"

namespace
{
	const size_t CgbFlagOffset = 0x143;
	const unsigned char CgbEnhanced = 0x80;
	const unsigned char CgbOnly = 0xc0;
}

GameBoyModel ModelForRom(const std::vector<unsigned char>& rom)
{
	if (rom.size() <= CgbFlagOffset) return GameBoyModel::Dmg;

	auto flag = rom[CgbFlagOffset];
	return flag == CgbEnhanced || flag == CgbOnly ? GameBoyModel::Cgb : GameBoyModel::Dmg;
}
