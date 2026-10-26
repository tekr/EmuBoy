#include "stdafx.h"
#include "BatteryStore.h"
#include "FileIo.h"
#include "SaveFile.h"
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <utility>

namespace
{
	BatteryStore::Result Failure(const std::string& action, const std::string& path, int error)
	{
		return { false, FileIo::FailureMessage(action, path, error) };
	}

	BatteryStore::Result WriteFileAtomically(const std::string& path, const std::vector<unsigned char>& data)
	{
		auto error = FileIo::WriteAtomically(path, data);
		return { error.empty(), error };
	}
}

int64_t BatteryStore::SystemTime()
{
	return static_cast<int64_t>(std::time(nullptr));
}

std::string BatteryStore::SavePathForRom(const std::string& romPath)
{
	return FileIo::ReplaceExtension(romPath, ".sav");
}

BatteryStore::BatteryStore(std::shared_ptr<Cartridge> cartridge, std::string savePath, TimeSource timeSource) :
	_cartridge{ std::move(cartridge) }, _savePath{ std::move(savePath) }, _timeSource{ std::move(timeSource) }
{
}

bool BatteryStore::IsNeeded() const
{
	return _cartridge->HasBattery() && (!_cartridge->GetRam().empty() || _cartridge->GetRealTimeClock() != nullptr);
}

BatteryStore::Result BatteryStore::Load()
{
	if (!IsNeeded()) return { true, "" };

	std::vector<unsigned char> data;
	if (!FileIo::ReadWholeFile(_savePath, data))
	{
		if (errno == ENOENT) return { true, "" };

		_loadFailed = true;
		return Failure("read", _savePath, errno);
	}

	_loadFailed = false;
	auto warning = SaveFile::Deserialise(*_cartridge, data, _timeSource());
	if (warning.empty()) return { true, "" };

	_needsBackup = true;
	_saveToBackUp = std::move(data);
	return { true, _savePath + ": " + warning + "; the original is copied to " + _savePath + ".bak before it is replaced" };
}

BatteryStore::Result BatteryStore::Save()
{
	if (!IsNeeded()) return { true, "" };

	return ReportOnce(Write());
}

BatteryStore::Result BatteryStore::Write()
{
	if (_loadFailed) return { false, "Not saving to " + _savePath + ", as it couldn't be loaded and would be overwritten" };

	if (_needsBackup)
	{
		auto backup = WriteFileAtomically(_savePath + ".bak", _saveToBackUp);
		if (!backup.Succeeded) return { false, "Not saving to " + _savePath + ", as it couldn't be backed up. " + backup.Message };

		_needsBackup = false;
		_saveToBackUp.clear();
	}

	auto result = WriteFileAtomically(_savePath, SaveFile::Serialise(*_cartridge, _timeSource()));
	if (result.Succeeded) _cartridge->MarkSaved();
	return result;
}

BatteryStore::Result BatteryStore::ReportOnce(Result result)
{
	if (result.Succeeded)
	{
		_lastError.clear();
	}
	else if (result.Message == _lastError)
	{
		result.Message.clear();
	}
	else
	{
		_lastError = result.Message;
	}

	return result;
}

BatteryStore::Result BatteryStore::SaveIfChanged()
{
	return _cartridge->HasUnsavedChanges() ? Save() : Result{ true, "" };
}

BatteryStore::Result BatteryStore::KeepSaveBeforeStateLoad()
{
	if (!IsNeeded()) return { true, "" };

	// The save is only written every few seconds, and not while paused, so the RAM may be newer
	auto saved = SaveIfChanged();
	if (!saved.Succeeded)
	{
		return { false, saved.Message.empty() ? "Couldn't save the game to " + _savePath : saved.Message };
	}

	auto copyPath = GetStateLoadCopyPath();
	std::vector<unsigned char> data;
	if (!FileIo::ReadWholeFile(_savePath, data))
	{
		if (errno != ENOENT) return Failure("read", _savePath, errno);

		// The game hasn't saved, so a copy from an earlier load would be misleading
		std::remove(copyPath.c_str());
		return { true, "" };
	}

	auto error = FileIo::WriteAtomically(copyPath, data);
	return { error.empty(), error };
}

BatteryStore::Result BatteryStore::SaveOnExit()
{
	return _cartridge->GetRealTimeClock() != nullptr ? Save() : SaveIfChanged();
}
