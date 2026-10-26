#pragma once
#include <functional>
#include <string>

// Chooses screenshot file names that don't overwrite earlier screenshots
class ScreenshotNamer
{
public:
	using FileExists = std::function<bool(const std::string&)>;

	static bool FileExistsOnDisk(const std::string& path);

	// The ROM's path without its extension (only a dot in the file name starts one), to name screenshots after
	static std::string BaseForRom(const std::string& romPath);

	// The ROM's file name without its directory or extension, for screenshots in the working directory
	static std::string NameForRom(const std::string& romPath);

	// "<base>-1.png", or the next number up that doesn't exist. Empty if every number is taken
	static std::string NextPath(const std::string& base, const FileExists& exists = FileExistsOnDisk);
};
