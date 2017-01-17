#pragma once
#include "../core/Cpu.h"
#include "TestMemoryMap.h"

class TestCpu : public Cpu
{
public:
	explicit TestCpu(TestMemoryMap& memory) : Cpu(memory) {}
	~TestCpu() = default;

	Registers& Registers() { return _registers; }

	bool& InterruptsEnabled() { return _interruptsEnabled; }
	unsigned char& EnabledInterrupts() { return _enabledInterrupts; }
	unsigned char& WaitingInterrupts() { return _waitingInterrupts; }
};

