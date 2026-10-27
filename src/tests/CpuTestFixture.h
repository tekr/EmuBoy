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
	TestMemoryMap MemoryMap { Joypad };
	TestCpu Cpu{ MemoryMap };

	CpuTestFixture() {}
	~CpuTestFixture();

	void Step(int instructions)
	{
		for (auto i = 0; i < instructions; i++) Cpu.DoNextInstruction();
	}
};

