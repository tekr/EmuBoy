#include "stdafx.h"
#include "FileIo.h"
#include <cerrno>
#include <cstdio>
#include <system_error>

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

	// Makes sure the written data has reached the disk before the file is renamed over the old one
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
}

std::string FileIo::FailureMessage(const std::string& action, const std::string& path, int error)
{
	// generic_category gives strerror's text without MSVC's deprecation error for strerror (C4996 is an error with
	// /sdl)
	return "Couldn't " + action + " " + path + ": " + std::generic_category().message(error);
}

bool FileIo::ReadWholeFile(const std::string& path, std::vector<unsigned char>& contents)
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

std::string FileIo::WriteAtomically(const std::string& path, const std::vector<unsigned char>& data)
{
	auto tempPath = path + ".tmp";

	auto file = OpenFile(tempPath, "wb");
	if (file == nullptr) return FailureMessage("create", tempPath, errno);

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
		return FailureMessage("write", tempPath, error);
	}

	if (!ReplaceAtomically(tempPath, path))
	{
		error = errno;
		std::remove(tempPath.c_str());
		return FailureMessage("replace", path, error);
	}

	return "";
}

std::string FileIo::ReplaceExtension(const std::string& path, const std::string& extension)
{
	auto nameStart = path.find_last_of("/\\");
	nameStart = nameStart == std::string::npos ? 0 : nameStart + 1;

	auto dot = path.rfind('.');
	auto stem = dot != std::string::npos && dot > nameStart ? path.substr(0, dot) : path;
	return stem + extension;
}
