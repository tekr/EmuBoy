#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include <gtest/gtest.h>
#include "../core/Graphics.h"
#include "../core/SpriteManager.h"

class InputJoypad;

class GraphicsTestFixture : public testing::Test
{
public:
	// Members named after their types qualify the type with :: so the name's meaning doesn't change within the
	// class, which GCC rejects
	InputJoypad Joypad;
	TestMemoryMap MemoryMap { Joypad };
	TestCpu Cpu{ MemoryMap };
	::SpriteManager SpriteManager{};
	::Graphics Graphics{ Cpu, MemoryMap, SpriteManager };

	GraphicsTestFixture() {}
	~GraphicsTestFixture();
};

