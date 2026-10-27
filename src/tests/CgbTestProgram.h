#pragma once
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <vector>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// A small hand-assembled program in a 32 KiB ROM whose header asks for the Game Boy Color, for tests of CGB mode.
// The CGB starts at $0100, where the ROM jumps over the header to the program at Start
class CgbTestProgram
{
	std::vector<unsigned char> _code;
	std::vector<std::pair<unsigned short, std::vector<unsigned char>>> _data;

public:
	static const unsigned short Start = 0x150;
	static const size_t CgbFlagOffset = 0x143;
	static const unsigned char CgbOnly = 0xc0;

	// The address of the next instruction
	unsigned short Here() const { return static_cast<unsigned short>(Start + _code.size()); }

	CgbTestProgram& Op(std::initializer_list<unsigned char> bytes)
	{
		_code.insert(_code.end(), bytes);
		return *this;
	}

	// LD A,value; LDH ($FF00+reg),A
	CgbTestProgram& WriteIo(unsigned char reg, unsigned char value) { return Op({ 0x3e, value, 0xe0, reg }); }

	// LDH A,($FF00+reg)
	CgbTestProgram& ReadIo(unsigned char reg) { return Op({ 0xf0, reg }); }

	CgbTestProgram& Nops(int count)
	{
		_code.insert(_code.end(), count, 0x00);
		return *this;
	}

	// JR $
	CgbTestProgram& Loop() { return Op({ 0x18, 0xfe }); }

	// Bytes placed in the ROM outside the program, e.g. as a source for VRAM DMA
	CgbTestProgram& Data(unsigned short address, std::vector<unsigned char> bytes)
	{
		_data.emplace_back(address, std::move(bytes));
		return *this;
	}

	std::vector<unsigned char> Rom() const
	{
		std::vector<unsigned char> rom(0x8000);
		const unsigned char jumpToStart[] = { 0xc3, Start & 0xff, Start >> 8 };
		std::copy(std::begin(jumpToStart), std::end(jumpToStart), rom.begin() + 0x100);
		rom[CgbFlagOffset] = CgbOnly;

		for (auto& data : _data) std::copy(data.second.begin(), data.second.end(), rom.begin() + data.first);
		std::copy(_code.begin(), _code.end(), rom.begin() + Start);
		return rom;
	}

	std::unique_ptr<Emulator> PowerOn(GameBoyModel model = GameBoyModel::Cgb) const
	{
		return std::make_unique<Emulator>(CartridgeFactory::Create(Rom()), model);
	}

	// The same program on the DMG, to compare. The DMG boot ROM would need a valid header, so it hands over at once, as
	// it does by writing to $FF50, and the CPU runs NOPs from $0000 to the jump at $0100
	std::unique_ptr<Emulator> PowerOnDmgWithoutBootRom() const
	{
		auto emulator = PowerOn(GameBoyModel::Dmg);
		emulator->GetMemoryMap().WriteByte(MemoryMap::InternalRomDisable, 1);
		return emulator;
	}
};

// Runs instructions until PC reaches the address; false if it doesn't within the limit
inline bool RunToAddress(Emulator& emulator, unsigned short address, int maxInstructions = 1000000)
{
	for (auto i = 0; i < maxInstructions; i++)
	{
		if (emulator.GetCpu().GetRegisters().PC == address) return true;
		emulator.RunInstruction();
	}

	return emulator.GetCpu().GetRegisters().PC == address;
}
