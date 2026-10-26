#pragma once
#include "../core/Emulator.h"
#include <string>

// Numbered save state slots for a game, each a file next to its ROM named after it, with the slot number in the
// extension (Tetris.gb's slot 1 is Tetris.ss1), as mGBA names them. One slot is selected at a time, for the save
// and load keys
class SaveStateSlots
{
public:
	static const int SlotCount = 4;

	struct Result
	{
		bool Succeeded;

		// What happened, to show the player
		std::string Message;
	};

	explicit SaveStateSlots(const std::string& romPath) : _romPath(romPath) {}

	// 1 to SlotCount
	int GetSlot() const { return _slot; }

	// Wrap around from the last slot to the first, and back
	void NextSlot() { _slot = _slot % SlotCount + 1; }
	void PreviousSlot() { _slot = (_slot + SlotCount - 2) % SlotCount + 1; }

	std::string PathForSlot(int slot) const;

	// Saves the emulator's state in the selected slot, replacing any state there
	Result Save(const Emulator& emulator) const;

	// Restores the emulator to the state in the selected slot. If that fails, the emulator is left as it was
	Result Load(Emulator& emulator) const;

private:
	std::string _romPath;
	int _slot{ 1 };
};
