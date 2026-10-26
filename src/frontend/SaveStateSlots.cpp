#include "SaveStateSlots.h"
#include "../core/FileIo.h"
#include "../core/SaveState.h"
#include <cerrno>

const int SaveStateSlots::SlotCount;

std::string SaveStateSlots::PathForSlot(int slot) const
{
	return FileIo::ReplaceExtension(_romPath, ".ss" + std::to_string(slot));
}

SaveStateSlots::Result SaveStateSlots::Save(const Emulator& emulator) const
{
	auto slot = std::to_string(_slot);
	auto error = FileIo::WriteAtomically(PathForSlot(_slot), SaveState::Create(emulator));
	if (!error.empty()) return { false, "Couldn't save state " + slot + ". " + error };

	return { true, "Saved state " + slot };
}

SaveStateSlots::Result SaveStateSlots::Load(Emulator& emulator) const
{
	auto slot = std::to_string(_slot);
	auto path = PathForSlot(_slot);

	std::vector<unsigned char> state;
	if (!FileIo::ReadWholeFile(path, state))
	{
		if (errno == ENOENT) return { false, "No state saved in slot " + slot };
		return { false, "Couldn't load state " + slot + ". " + FileIo::FailureMessage("read", path, errno) };
	}

	auto error = SaveState::Restore(emulator, state);
	if (!error.empty()) return { false, "Couldn't load state " + slot + " from " + path + ". " + error };

	return { true, "Loaded state " + slot };
}
