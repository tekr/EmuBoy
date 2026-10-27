#include "stdafx.h"
#include "CgbColourCorrection.h"
#include <algorithm>
#include <cmath>

namespace
{
	// Pokefan531's measured model of the GBC's screen, from his Handheld-Colorspace-Shaders at commit a21d4d4
	// (https://github.com/Pokefan531/Handheld-Colorspace-Shaders/tree/a21d4d4154af1dd87588febe545e6d9f0555ec8f), whose
	// preset runs two shaders in Slang/handheld/shaders/color, and whose numbers are used here in code of Emu Boy's own:
	// - the gamma ramp (GbcGamma) is his "GBCGamma" table from gbc-gamma.slang, which has no licence line (its header
	//   says it's based on the ss-gamma-ramp shader by Overload's ramp from bsnes, modified by Pokefan531);
	// - the matrix and luminance are gbc-color.slang's sRGB profile; that file is marked "License: Public domain".
	// He measured the bare LCD, without its case or glass, with a spectrophotometer.
	//
	// Each 5-bit channel first goes through the screen's measured response (GbcGamma, his "GBCGamma" ramp), then is
	// linearised with a gamma of 2.2 and dimmed to 89.89% luminance, mixed between channels by the measured matrix, and
	// encoded again with the display's gamma of 2.2. Against raw RGB scaled to 8 bits, white is a little dimmer, colours
	// are less saturated and slightly mixed, and the mid-tones are lighter. The shader clamps only before mixing, where
	// a negative mix would be undefined; here the mix is clamped too
	const int GbcGamma[32] =
	{
		0, 8, 15, 26, 37, 49, 67, 85, 97, 110, 121, 134, 140, 148, 154, 160,
		170, 180, 188, 196, 202, 207, 212, 217, 222, 228, 232, 237, 246, 252, 254, 255
	};

	const double Gamma = 2.2;
	const double Luminance = 0.8989;

	// Rows: the red, green and blue output; columns: how much of the red, green and blue input goes into it
	const double Matrix[3][3] =
	{
		{ 0.8944, 0.2179, -0.1125 },
		{ 0.1091, 0.6312, 0.2597 },
		{ 0.1447, 0.2837, 0.5716 }
	};

	int Correct(int rgb555)
	{
		double linear[3];
		for (auto channel = 0; channel < 3; channel++)
		{
			auto value = GbcGamma[(rgb555 >> (channel * 5)) & 0x1f];
			linear[channel] = std::pow(value / 255.0, Gamma) * Luminance;
		}

		auto pixel = 0;
		for (auto channel = 0; channel < 3; channel++)
		{
			auto mixed = Matrix[channel][0] * linear[0] + Matrix[channel][1] * linear[1] + Matrix[channel][2] * linear[2];
			auto encoded = std::pow(std::min(std::max(mixed, 0.0), 1.0), 1 / Gamma);
			pixel |= static_cast<int>(std::lround(encoded * 255)) << (channel * 8);
		}

		return static_cast<int>(CgbColourCorrection::FrameOpacity | static_cast<unsigned int>(pixel));
	}

	std::array<int, CgbColourCorrection::ColourCount> BuildTable()
	{
		std::array<int, CgbColourCorrection::ColourCount> table{};
		for (auto colour = 0; colour < CgbColourCorrection::ColourCount; colour++) table[colour] = Correct(colour);
		return table;
	}
}

const std::array<int, CgbColourCorrection::ColourCount>& CgbColourCorrection::Table()
{
	static const auto table = BuildTable();
	return table;
}
