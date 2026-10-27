#pragma once
#include <array>

// The colours the CGB's screen shows, from Pokefan531's measurements of its reflective, unlit LCD (see the .cpp). Against
// the same 15-bit values shown as raw RGB on a backlit display, white is a little dimmer, colours are less saturated
// and slightly mixed between channels, and the mid-tones are lighter. Pixels are in the front end's format: bytes R, G,
// B, A in memory, so 0xAABBGGRR as an int.
class CgbColourCorrection
{
public:
	static const int ColourCount = 1 << 15;

	// The frame is drawn at 80% opacity over the previous ones. The CGB's LCD responds faster than the DMG's (drawn
	// at 60%), so half as much of the last frame lingers (20% rather than 40%), but it doesn't settle within a frame
	// either: a trace of the last frame keeps the blending that games which flicker sprites rely on. The LCD-off
	// colour is drawn the same way, so the screen fades to it over a few frames rather than at once as on the DMG
	static const unsigned int FrameOpacity = 0xcc000000;

	// The pixel for a 15-bit colour (red in bits 0-4, green 5-9, blue 10-14; bit 15 is ignored)
	static int Pixel(unsigned short rgb555) { return Table()[rgb555 & (ColourCount - 1)]; }

	// The screen with the LCD off: the panel then lets through as much light as it can, the model's white
	static int LcdOffPixel() { return Pixel(0x7fff); }

	// The whole table, for drawing a line without looking it up afresh for each pixel
	static const std::array<int, ColourCount>& Table();
};
