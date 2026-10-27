#pragma once
#include "TestMemoryMap.h"
#include "TestCpu.h"
#include <gtest/gtest.h>

class CpuTestFixture : public testing::Test
{
public:
	static constexpr int MachineCycle = 4;
	static constexpr unsigned short StackTop = 0xc010;

	InputJoypad Joypad;
	TestMemoryMap Memory { Joypad };
	TestCpu Processor{ Memory };

	CpuTestFixture() {}
	~CpuTestFixture();

	void Step(int instructions)
	{
		for (auto i = 0; i < instructions; i++) Processor.DoNextInstruction();
	}
};

