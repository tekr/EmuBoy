#include "stdafx.h"
#include "BatteryStore.h"
#include "SaveFile.h"
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
	BatteryStore::Result Failure(const std::string& action, const std::string& path, int error)
	{
		// generic_category gives strerror's text without MSVC's deprecation error for strerror (C4996 is an error
		// with /sdl)
		return { false, "Couldn't " + action + " " + path + ": " + std::generic_category().message(error) };
	}

	// fopen, without MSVC's deprecation error. Returns null with errno set on failure
	std::FILE* OpenFile(const std::string& path, const char* mode)
	{
#ifdef _MSC_VER
		std::FILE* file = nullptr;
		auto error = fopen_s(&file, path.c_str(), mode);
		if (error != 0) errno = error;
		return file;
#else
		return std::fopen(path.c_str(), mode);
#endif
	}

	// Reads the whole file. Returns false with errno set if it can't
	bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& contents)
	{
		auto file = OpenFile(path, "rb");
		if (file == nullptr) return false;

		unsigned char buffer[4096];
		size_t count;
		while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) contents.insert(contents.end(), buffer, buffer + count);

		auto failed = std::ferror(file) != 0;
		auto error = errno;
		std::fclose(file);
		errno = error;
		return !failed;
	}

	// Makes sure the written data has reached the disk before the file is renamed over the old save
	bool FlushToDisk(std::FILE* file)
	{
		if (std::fflush(file) != 0) return false;
#ifdef _WIN32
		return _commit(_fileno(file)) == 0;
#else
		return fsync(fileno(file)) == 0;
#endif
	}

	// Renames over any existing file in one step, so the destination is always either the old or the new file
	bool ReplaceAtomically(const std::string& from, const std::string& to)
	{
#ifdef _WIN32
		if (MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;

		errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : EIO;
		return false;
#else
		return std::rename(from.c_str(), to.c_str()) == 0;
#endif
	}

	BatteryStore::Result WriteFileAtomically(const std::string& path, const std::vector<unsigned char>& data)
	{
		auto tempPath = path + ".tmp";

		auto file = OpenFile(tempPath, "wb");
		if (file == nullptr) return Failure("create", tempPath, errno);

		auto written = std::fwrite(data.data(), 1, data.size(), file) == data.size() && FlushToDisk(file);
		auto error = errno;
		if (std::fclose(file) != 0 && written)
		{
			written = false;
			error = errno;
		}

		if (!written)
		{
			std::remove(tempPath.c_str());
			return Failure("write", tempPath, error);
		}

		if (!ReplaceAtomically(tempPath, path))
		{
			error = errno;
			std::remove(tempPath.c_str());
			return Failure("replace", path, error);
		}

		return { true, "" };
	}
}

int64_t BatteryStore::SystemTime()
{
	return static_cast<int64_t>(std::time(nullptr));
}

std::string BatteryStore::SavePathForRom(const std::string& romPath)
{
	// Only a dot in the file name starts an extension, not one in a directory name
	auto nameStart = romPath.find_last_of("/\\");
	nameStart = nameStart == std::string::npos ? 0 : nameStart + 1;

	auto dot = romPath.rfind('.');
	auto stem = dot != std::string::npos && dot > nameStart ? romPath.substr(0, dot) : romPath;
	return stem + ".sav";
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
	if (!ReadWholeFile(_savePath, data))
	{
		if (errno == ENOENT) return { true, "" };

		_loadFailed = true;
		return Failure("read", _savePath, errno);
	}

	_loadFailed = false;
	auto warning = SaveFile::Deserialise(*_cartridge, data, _timeSource());
	return { true, warning.empty() ? "" : _savePath + ": " + warning };
}

BatteryStore::Result BatteryStore::Save()
{
	if (!IsNeeded()) return { true, "" };

	if (_loadFailed) return { false, "Not saving to " + _savePath + ", as it couldn't be loaded and would be overwritten" };

	auto result = WriteFileAtomically(_savePath, SaveFile::Serialise(*_cartridge, _timeSource()));
	if (result.Succeeded) _cartridge->MarkSaved();
	return result;
}

BatteryStore::Result BatteryStore::SaveIfChanged()
{
	// A failed load was reported when it happened, so needn't be repeated on every attempt
	if (_loadFailed || !_cartridge->HasUnsavedChanges()) return { true, "" };

	return Save();
}
