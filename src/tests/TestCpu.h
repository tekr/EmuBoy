#pragma once
#include "../core/Cpu.h"
#include "TestMemoryMap.h"

class TestCpu : public Cpu
{
public:
	explicit TestCpu(TestMemoryMap& memory) : Cpu(memory) {}
	~TestCpu() = default;

	// Qualified with :: because GCC rejects a member that changes the meaning of the name Registers
	::Registers& Registers() { return _registers; }
	CpuState& State() { return _state; }

	bool& InterruptsEnabled() { return _interruptsEnabled; }
	unsigned char& EnabledInterrupts() { return _enabledInterrupts; }
	unsigned char& WaitingInterrupts() { return _waitingInterrupts; }
};

