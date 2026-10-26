#include "Letterbox.h"

Letterbox Letterbox::Fit(unsigned windowWidth, unsigned windowHeight, unsigned contentWidth, unsigned contentHeight)
{
	if (windowWidth == 0 || windowHeight == 0) return { 0.f, 0.f, 1.f, 1.f };

	// Compare the aspect ratios without dividing, so exact fits come out exact
	auto windowWider = static_cast<unsigned long long>(windowWidth) * contentHeight >
		static_cast<unsigned long long>(windowHeight) * contentWidth;

	if (windowWider)
	{
		auto width = static_cast<float>(windowHeight) * contentWidth / contentHeight / windowWidth;
		return { (1.f - width) / 2, 0.f, width, 1.f };
	}

	auto height = static_cast<float>(windowWidth) * contentHeight / contentWidth / windowHeight;
	return { 0.f, (1.f - height) / 2, 1.f, height };
}
