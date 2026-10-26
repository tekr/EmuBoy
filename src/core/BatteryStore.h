#pragma once
#include "Cartridge.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Keeps a cartridge's battery-backed RAM and clock in a save file (in SaveFile's format) between runs. Cartridges
// without a battery, or with nothing for it to keep, never touch the file system
class BatteryStore
{
public:
	// The current UNIX time in seconds; injectable so tests can control the time that passes between runs
	using TimeSource = std::function<int64_t()>;

	struct Result
	{
		bool Succeeded;

		// An error, or a warning when Succeeded; empty if there is nothing to report
		std::string Message;
	};

	static int64_t SystemTime();

	// <rom path without extension>.sav, as other emulators name it
	static std::string SavePathForRom(const std::string& romPath);

	BatteryStore(std::shared_ptr<Cartridge> cartridge, std::string savePath, TimeSource timeSource = SystemTime);

	bool IsNeeded() const;
	const std::string& GetSavePath() const { return _savePath; }

	// Loads the save into the cartridge. A missing save leaves the RAM blank
	Result Load();

	// Writes the save to a temporary file and renames it over the old one, so an interrupted save can't corrupt it.
	// A save that loaded with a warning (being the wrong size) is first copied to <save path>.bak, as it may hold
	// data that didn't fit. An error the same as the last one is returned without its message, so the front end
	// reports it once rather than on every attempt, until a save succeeds again
	Result Save();

	// Saves only if the RAM or clock registers changed since the last load or save
	Result SaveIfChanged();

	// Copies the save as it is now to <save path>.bak before the save is next replaced, as when a save state that
	// rolls the RAM back has been loaded. Only the first call does so, so the backup is the save from before any
	// state was loaded
	Result BackUpBeforeNextSave();

	// Saves when the emulator exits: only if something changed, so just running a game never rewrites its save,
	// except that a clock cartridge's save is always rewritten to keep its time stamp current
	Result SaveOnExit();

private:
	std::shared_ptr<Cartridge> _cartridge;
	std::string _savePath;
	TimeSource _timeSource;

	// Set when a save exists but couldn't be read, so that it isn't overwritten with blank RAM
	bool _loadFailed{ false };

	// The save as loaded, kept when it loaded with a warning until it has been backed up
	std::vector<unsigned char> _saveToBackUp;
	bool _needsBackup{ false };
	bool _backedUpForState{ false };

	std::string _lastError;

	Result Write();
	Result ReportOnce(Result result);
};
