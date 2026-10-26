#include "ScreenshotNamer.h"
#include <cstdio>

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
#ifdef _MSC_VER
	std::FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0) return false;
#else
	auto file = std::fopen(path.c_str(), "rb");
	if (file == nullptr) return false;
#endif
	std::fclose(file);
	return true;
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
