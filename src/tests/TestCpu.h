#pragma once
#include "../core/Cpu.h"
#include "TestMemoryMap.h"

class TestCpu : public Cpu
{
public:
	explicit TestCpu(TestMemoryMap& memory) : Cpu(memory) {}
	~TestCpu() = default;

	// Tests can change the registers as well as read them through the const accessor
	using Cpu::GetRegisters;
	Registers& GetRegisters() { return _registers; }
	CpuState& State() { return _state; }

	bool& InterruptsEnabled() { return _interruptsEnabled; }
	unsigned char& EnabledInterrupts() { return _enabledInterrupts; }
	unsigned char& WaitingInterrupts() { return _waitingInterrupts; }
};

