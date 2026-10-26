#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// Helpers for tests that read and write files
namespace TestFiles
{
	inline bool MakeTestDirectory(const std::string& path)
	{
#ifdef _WIN32
		return _mkdir(path.c_str()) == 0;
#else
		return mkdir(path.c_str(), 0700) == 0;
#endif
	}

	inline void RemoveTestDirectory(const std::string& path)
	{
#ifdef _WIN32
		_rmdir(path.c_str());
#else
		rmdir(path.c_str());
#endif
	}

	// The system's directory for temporary files, read without MSVC's deprecation error for getenv
	inline std::string TempDirectory()
	{
		for (auto name : { "TMPDIR", "TEMP" })
		{
#ifdef _MSC_VER
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) continue;

			std::string directory(value);
			free(value);
			return directory;
#else
			if (auto value = std::getenv(name)) return value;
#endif
		}

#ifdef _WIN32
		return ".";
#else
		return "/tmp";
#endif
	}

	inline bool Exists(const std::string& path)
	{
		std::ifstream file(path);
		return file.good();
	}

	inline std::vector<unsigned char> ReadAll(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	inline void WriteAll(const std::string& path, const std::vector<unsigned char>& data)
	{
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(data.data()), data.size());
	}

	// A fresh directory for a test's files, removed afterwards along with the files (and their temporary files)
	// named through File
	class TestDirectory
	{
		std::string _path;
		std::vector<std::string> _files;

	public:
		explicit TestDirectory(const std::string& name)
		{
			static int next = 0;
			_path = TempDirectory() + "/EmuBoy" + name + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(next++);
			MakeTestDirectory(_path);
		}

		~TestDirectory()
		{
			for (auto& file : _files)
			{
				std::remove(file.c_str());
				std::remove((file + ".tmp").c_str());
			}

			RemoveTestDirectory(_path);
		}

		TestDirectory(const TestDirectory&) = delete;
		TestDirectory& operator=(const TestDirectory&) = delete;

		const std::string& Path() const { return _path; }

		std::string File(const std::string& name)
		{
			_files.push_back(_path + "/" + name);
			return _files.back();
		}
	};
}
