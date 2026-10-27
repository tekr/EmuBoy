#include "stdafx.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// Runs Matt Currie's dmg-acid2 (see roms/readme.md) and compares the frame with the reference image, stored as
// one line of shade digits (0 = lightest, 3 = darkest) per screen row.

namespace
{
	// The boot ROM and the test take about 6 s of emulated time; this leaves plenty of margin
	const int MaxFrames = 60 * 20;

	// Frames that must still match after the first one does
	const int FramesToStayMatched = 10;

	std::vector<std::string> LoadReference(const std::string& path)
	{
		std::ifstream file(path);
		std::vector<std::string> rows;

		for (std::string row; std::getline(file, row);)
		{
			rows.push_back(row);
		}

		return rows;
	}

	char ShadeDigit(int colour)
	{
		for (unsigned char shade = 0; shade < 4; shade++)
		{
			if (Graphics::ShadeColour(shade) == colour) return static_cast<char>('0' + shade);
		}

		return '?';
	}

	// Returns the frame as rows of shade digits, in the reference's format
	std::vector<std::string> ToShadeRows(const int* frame)
	{
		std::vector<std::string> rows(Graphics::VertPixels);

		for (auto y = 0; y < Graphics::VertPixels; y++)
		{
			for (auto x = 0; x < Graphics::HozPixels; x++)
			{
				rows[y] += ShadeDigit(frame[y * Graphics::HozPixels + x]);
			}
		}

		return rows;
	}

	std::string DescribeDifferences(const std::vector<std::string>& actual, const std::vector<std::string>& reference)
	{
		std::ostringstream diff;
		diff << "(shades 0-3, ? = not a DMG shade)\n";

		for (auto y = 0; y < Graphics::VertPixels; y++)
		{
			if (actual[y] != reference[y])
			{
				diff << "row " << y << "\n  expected " << reference[y] << "\n  actual   " << actual[y] << "\n";
			}
		}

		return diff.str();
	}
}

TEST(DmgAcid2Test, FrameMatchesReferenceImage)
{
	auto cartridge = CartridgeFactory::LoadFromFileWithRamSize("roms/dmg-acid2/dmg-acid2.gb", Cartridge::RamBankSize);
	ASSERT_NE(nullptr, cartridge) << "ROM not found; run the tests from src/tests";

	auto reference = LoadReference("roms/dmg-acid2/reference-dmg.txt");
	ASSERT_EQ(static_cast<size_t>(Graphics::VertPixels), reference.size());

	Emulator emulator{ cartridge };
	std::vector<std::string> actual;
	auto frame = 0;

	for (; frame < MaxFrames && actual != reference; frame++)
	{
		actual = ToShadeRows(emulator.GetFrame());
	}

	ASSERT_TRUE(actual == reference) << "No frame matched the reference image; the last differs " << DescribeDifferences(actual, reference);

	// The final image doesn't change once drawn
	for (auto i = 0; i < FramesToStayMatched; i++, frame++)
	{
		actual = ToShadeRows(emulator.GetFrame());
		ASSERT_TRUE(actual == reference) << "Frame " << frame << " differs " << DescribeDifferences(actual, reference);
	}
}
