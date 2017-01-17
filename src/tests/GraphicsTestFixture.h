#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include <gtest/gtest.h>
#include "../core/Graphics.h"

class GraphicsTestFixture : public testing::Test
{
public:
	TestMemoryMap MemoryMap;
	TestCpu Cpu{ MemoryMap };
	Graphics Graphics{ Cpu, MemoryMap };

	GraphicsTestFixture() {}
	~GraphicsTestFixture();
};

