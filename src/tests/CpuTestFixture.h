#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include <gtest/gtest.h>

class CpuTestFixture : public testing::Test
{
public:
	InputJoypad Joypad;
	TestMemoryMap MemoryMap { Joypad };
	TestCpu Cpu{ MemoryMap };

	CpuTestFixture() {}
	~CpuTestFixture();
};

