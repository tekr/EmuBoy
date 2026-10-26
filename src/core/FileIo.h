#pragma once
#include <string>
#include <vector>

// Reading and writing whole files, as battery saves and save states are
class FileIo
{
public:
	// Reads the whole file. Returns false with errno set if it can't
	static bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& contents);

	// Writes the file to <path>.tmp, flushes it to disk and renames it over the old file, so the file is always
	// either the old one or the new one, even if writing is interrupted. Returns an empty string, or what went wrong
	static std::string WriteAtomically(const std::string& path, const std::vector<unsigned char>& data);

	// "Couldn't <action> <path>: <the error's description>"
	static std::string FailureMessage(const std::string& action, const std::string& path, int error);

	// The path with its file name's extension, if any, replaced; a dot in a directory name doesn't start one
	static std::string ReplaceExtension(const std::string& path, const std::string& extension);
};
