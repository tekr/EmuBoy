#pragma once
#include "Cartridge.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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

	// Writes the save to a temporary file and renames it over the old one, so an interrupted save can't corrupt it
	Result Save();

	// Saves only if the RAM or clock registers changed since the last load or save. Quietly does nothing after a
	// failed load, which Load and Save report
	Result SaveIfChanged();

private:
	std::shared_ptr<Cartridge> _cartridge;
	std::string _savePath;
	TimeSource _timeSource;

	// Set when a save exists but couldn't be read, so that it isn't overwritten with blank RAM
	bool _loadFailed{ false };
};
