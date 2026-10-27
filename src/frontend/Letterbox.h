#pragma once

// Where to draw the screen in a window of any shape: as large as fits while keeping its aspect ratio, centred, with
// bars on the other sides
struct Letterbox
{
	// Fractions of the window's width and height
	float Left;
	float Top;
	float Width;
	float Height;

	static Letterbox Fit(unsigned windowWidth, unsigned windowHeight, unsigned contentWidth, unsigned contentHeight);
};
