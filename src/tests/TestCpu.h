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
	uint8_t& EnabledInterrupts() { return _enabledInterrupts; }
	uint8_t& WaitingInterrupts() { return _waitingInterrupts; }
};

