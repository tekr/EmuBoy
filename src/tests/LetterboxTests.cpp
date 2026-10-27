#include "stdafx.h"
#include <gtest/gtest.h>
#include "../frontend/Letterbox.h"

namespace
{
	void ExpectBox(float left, float top, float width, float height, const Letterbox& box)
	{
		EXPECT_FLOAT_EQ(left, box.Left);
		EXPECT_FLOAT_EQ(top, box.Top);
		EXPECT_FLOAT_EQ(width, box.Width);
		EXPECT_FLOAT_EQ(height, box.Height);
	}
}

TEST(LetterboxTest, SameAspectRatioFillsWindow)
{
	ExpectBox(0.f, 0.f, 1.f, 1.f, Letterbox::Fit(640, 576, 160, 144));
	ExpectBox(0.f, 0.f, 1.f, 1.f, Letterbox::Fit(160, 144, 160, 144));
}

TEST(LetterboxTest, WiderWindowHasBarsAtSides)
{
	// 1920x1080 fits 1200x1080 of 160x144, leaving 360 px each side
	ExpectBox(360.f / 1920, 0.f, 1200.f / 1920, 1.f, Letterbox::Fit(1920, 1080, 160, 144));
}

TEST(LetterboxTest, TallerWindowHasBarsAboveAndBelow)
{
	// 800x1000 fits 800x720, leaving 140 px above and below
	ExpectBox(0.f, 140.f / 1000, 1.f, 720.f / 1000, Letterbox::Fit(800, 1000, 160, 144));
}

TEST(LetterboxTest, EmptyWindowFillsIt)
{
	ExpectBox(0.f, 0.f, 1.f, 1.f, Letterbox::Fit(0, 0, 160, 144));
	ExpectBox(0.f, 0.f, 1.f, 1.f, Letterbox::Fit(100, 0, 160, 144));
}
