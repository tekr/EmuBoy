#include "stdafx.h"
#include <gtest/gtest.h>
#include "../frontend/ScreenshotNamer.h"
#include <set>

namespace
{
	ScreenshotNamer::FileExists Existing(std::set<std::string> paths)
	{
		return [paths](const std::string& path) { return paths.count(path) != 0; };
	}
}

TEST(ScreenshotNamerTest, BaseIsRomPathWithoutExtension)
{
	EXPECT_EQ("roms/Tetris", ScreenshotNamer::BaseForRom("roms/Tetris.gb"));
	EXPECT_EQ("C:\\roms\\Tetris", ScreenshotNamer::BaseForRom("C:\\roms\\Tetris.gb"));
	EXPECT_EQ("Tetris", ScreenshotNamer::BaseForRom("Tetris"));
}

TEST(ScreenshotNamerTest, DotInDirectoryIsntExtension)
{
	EXPECT_EQ("my.roms/Tetris", ScreenshotNamer::BaseForRom("my.roms/Tetris"));
	EXPECT_EQ("my.roms/Tetris", ScreenshotNamer::BaseForRom("my.roms/Tetris.gb"));
}

TEST(ScreenshotNamerTest, OnlyLastExtensionRemoved)
{
	EXPECT_EQ("roms/Tetris.v1", ScreenshotNamer::BaseForRom("roms/Tetris.v1.gb"));
}

TEST(ScreenshotNamerTest, NameIsFileNameWithoutExtension)
{
	EXPECT_EQ("Tetris", ScreenshotNamer::NameForRom("/home/me/roms/Tetris.gb"));
	EXPECT_EQ("Tetris", ScreenshotNamer::NameForRom("C:\\roms\\Tetris.gb"));
	EXPECT_EQ("Tetris", ScreenshotNamer::NameForRom("Tetris.gb"));
}

TEST(ScreenshotNamerTest, FirstScreenshotIsNumberedOne)
{
	EXPECT_EQ("roms/Tetris-1.png", ScreenshotNamer::NextPath("roms/Tetris", Existing({})));
}

TEST(ScreenshotNamerTest, SkipsExistingFiles)
{
	EXPECT_EQ("Tetris-3.png", ScreenshotNamer::NextPath("Tetris", Existing({ "Tetris-1.png", "Tetris-2.png" })));
}

TEST(ScreenshotNamerTest, FillsGaps)
{
	EXPECT_EQ("Tetris-2.png", ScreenshotNamer::NextPath("Tetris", Existing({ "Tetris-1.png", "Tetris-3.png" })));
}

TEST(ScreenshotNamerTest, EmptyWhenEveryNumberIsTaken)
{
	EXPECT_EQ("", ScreenshotNamer::NextPath("Tetris", [](const std::string&) { return true; }));
}

TEST(ScreenshotNamerTest, ChecksFilesOnDisk)
{
	// Test ROMs are in the working directory the tests run in
	EXPECT_TRUE(ScreenshotNamer::FileExistsOnDisk("roms/readme.md"));
	EXPECT_FALSE(ScreenshotNamer::FileExistsOnDisk("roms/no-such-file.png"));
}
