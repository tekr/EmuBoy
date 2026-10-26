#include "ScreenshotNamer.h"
#include <sys/stat.h>
#include <sys/types.h>

namespace
{
	const int MaxScreenshots = 99999;

	size_t NameStart(const std::string& path)
	{
		auto slash = path.find_last_of("/\\");
		return slash == std::string::npos ? 0 : slash + 1;
	}
}

bool ScreenshotNamer::FileExistsOnDisk(const std::string& path)
{
	// stat rather than opening the file, so a file that can't be read still counts
#ifdef _MSC_VER
	struct _stat info;
	return _stat(path.c_str(), &info) == 0;
#else
	struct stat info;
	return stat(path.c_str(), &info) == 0;
#endif
}

std::string ScreenshotNamer::BaseForRom(const std::string& romPath)
{
	auto dot = romPath.rfind('.');
	return dot != std::string::npos && dot > NameStart(romPath) ? romPath.substr(0, dot) : romPath;
}

std::string ScreenshotNamer::NameForRom(const std::string& romPath)
{
	auto base = BaseForRom(romPath);
	return base.substr(NameStart(base));
}

std::string ScreenshotNamer::NextPath(const std::string& base, const FileExists& exists)
{
	for (auto number = 1; number <= MaxScreenshots; number++)
	{
		auto path = base + "-" + std::to_string(number) + ".png";
		if (!exists(path)) return path;
	}

	return "";
}
