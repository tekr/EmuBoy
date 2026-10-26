#include "stdafx.h"
#include <gtest/gtest.h>
#include <cerrno>
#include "../core/FileIo.h"
#include "TestFiles.h"

using namespace TestFiles;

TEST(FileIoTest, ReplacesTheExtension)
{
	EXPECT_EQ("roms/Tetris.ss1", FileIo::ReplaceExtension("roms/Tetris.gb", ".ss1"));
	EXPECT_EQ("C:\\games\\Zelda.sav", FileIo::ReplaceExtension("C:\\games\\Zelda.gbc", ".sav"));
	EXPECT_EQ("Game.v1.sav", FileIo::ReplaceExtension("Game.v1.gb", ".sav"));
}

TEST(FileIoTest, AddsAnExtensionToANameWithoutOne)
{
	EXPECT_EQ("roms/Tetris.sav", FileIo::ReplaceExtension("roms/Tetris", ".sav"));
	EXPECT_EQ("my.roms/Tetris.sav", FileIo::ReplaceExtension("my.roms/Tetris", ".sav"));
	EXPECT_EQ("my.roms\\Tetris.sav", FileIo::ReplaceExtension("my.roms\\Tetris", ".sav"));
}

TEST(FileIoTest, WritesAFileAndReadsItBack)
{
	TestDirectory directory{ "FileIoTest" };
	auto path = directory.File("data.bin");
	std::vector<unsigned char> data{ 0, 1, 2, 0xff, 0x80 };

	EXPECT_EQ("", FileIo::WriteAtomically(path, data));

	std::vector<unsigned char> read;
	EXPECT_TRUE(FileIo::ReadWholeFile(path, read));
	EXPECT_EQ(data, read);
	EXPECT_FALSE(Exists(path + ".tmp"));
}

TEST(FileIoTest, WritingReplacesTheOldFile)
{
	TestDirectory directory{ "FileIoTest" };
	auto path = directory.File("data.bin");
	WriteAll(path, std::vector<unsigned char>(100, 7));

	EXPECT_EQ("", FileIo::WriteAtomically(path, { 1, 2, 3 }));

	EXPECT_EQ((std::vector<unsigned char>{ 1, 2, 3 }), ReadAll(path));
}

TEST(FileIoTest, WritingIntoAMissingDirectoryReportsIt)
{
	TestDirectory directory{ "FileIoTest" };
	auto path = directory.Path() + "/missing/data.bin";

	auto error = FileIo::WriteAtomically(path, { 1 });

	EXPECT_EQ(0u, error.find("Couldn't create " + path + ".tmp: ")) << error;
	EXPECT_FALSE(Exists(path));
}

TEST(FileIoTest, ReadingAMissingFileFailsWithENOENT)
{
	TestDirectory directory{ "FileIoTest" };
	std::vector<unsigned char> read;

	EXPECT_FALSE(FileIo::ReadWholeFile(directory.Path() + "/missing.bin", read));
	EXPECT_EQ(ENOENT, errno);
}
