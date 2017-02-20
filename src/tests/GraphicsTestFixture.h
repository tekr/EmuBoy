#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include <gtest/gtest.h>
#include "../core/Graphics.h"
#include "../Core/SpriteManager.h"

class InputJoypad;

class GraphicsTestFixture : public testing::Test
{
public:
	InputJoypad Joypad;
	TestMemoryMap MemoryMap { Joypad };
	TestCpu Cpu{ MemoryMap };
	SpriteManager SpriteManager{};
	Graphics Graphics{ Cpu, MemoryMap, SpriteManager };

	GraphicsTestFixture() {}
	~GraphicsTestFixture();
};

