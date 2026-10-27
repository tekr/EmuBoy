#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../core/Emulator.h"
#include "../core/CartridgeFactory.h"

// Runs Matt Currie's cgb-acid2 (see roms/readme.md) and compares the frame, as 15-bit colours before colour
// correction, with the reference image, stored as one line per screen row of four hex digits per pixel.

namespace
{
	// The test draws its image within a few frames, as there's no boot ROM
	const int MaxFrames = 60 * 5;
	const int FramesToStayMatched = 10;

	std::vector<std::string> LoadReference(const std::string& path)
	{
		std::ifstream file(path);
		std::vector<std::string> rows;
		for (std::string row; std::getline(file, row);) rows.push_back(row);
		return rows;
	}

	std::vector<std::string> ToColourRows(const unsigned short* frame)
	{
		std::vector<std::string> rows(Graphics::VertPixels);

		for (auto y = 0u; y < Graphics::VertPixels; y++)
		{
			for (auto x = 0u; x < Graphics::HozPixels; x++)
			{
				char digits[5];
				std::snprintf(digits, sizeof(digits), "%04x", frame[y * Graphics::HozPixels + x]);
				rows[y] += digits;
			}
		}

		return rows;
	}

	std::string DescribeDifferences(const std::vector<std::string>& actual, const std::vector<std::string>& reference)
	{
		std::ostringstream diff;
		diff << "(15-bit colours, ffff = LCD off)\n";

		for (auto y = 0u; y < Graphics::VertPixels; y++)
		{
			if (actual[y] != reference[y]) diff << "row " << y << "\n  expected " << reference[y] << "\n  actual   " << actual[y] << "\n";
		}

		return diff.str();
	}
}

TEST(CgbAcid2Test, FrameMatchesReferenceImage)
{
	auto cartridge = CartridgeFactory::LoadFromFile("roms/cgb-acid2/cgb-acid2.gbc");
	ASSERT_NE(nullptr, cartridge) << "ROM not found; run the tests from src/tests";

	auto reference = LoadReference("roms/cgb-acid2/reference-cgb.txt");
	ASSERT_EQ(static_cast<size_t>(Graphics::VertPixels), reference.size());

	// The header's CGB flag ($C0) chooses the CGB
	Emulator emulator{ cartridge };
	ASSERT_EQ(GameBoyModel::Cgb, emulator.GetModel());

	std::vector<std::string> actual;
	auto frame = 0;

	for (; frame < MaxFrames && actual != reference; frame++)
	{
		emulator.GetFrame();
		actual = ToColourRows(emulator.GetCgbScreenRgb555());
	}

	ASSERT_TRUE(actual == reference) << "No frame matched the reference image; the last differs " << DescribeDifferences(actual, reference);

	for (auto i = 0; i < FramesToStayMatched; i++, frame++)
	{
		emulator.GetFrame();
		actual = ToColourRows(emulator.GetCgbScreenRgb555());
		ASSERT_TRUE(actual == reference) << "Frame " << frame << " differs " << DescribeDifferences(actual, reference);
	}
}
