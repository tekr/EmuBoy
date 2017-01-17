#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include <gtest/gtest.h>

class CpuTestFixture : public testing::Test
{
public:
	TestMemoryMap MemoryMap;
	TestCpu Cpu{ MemoryMap };

	CpuTestFixture() {}
	~CpuTestFixture();
};

