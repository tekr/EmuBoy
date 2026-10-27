#pragma once
#include "MemoryMap.h"
#include <vector>

// Avoids enum class to simplify bit-level operations
enum CpuFlags : unsigned char
{
	ZeroFlag = 0x80,
	SubFlag = 0x40,
	HalfCarryFlag = 0x20,
	CarryFlag = 0x10,
	NoFlags = 0x00,
	AllFlags = 0xff
};

// Avoids enum class to simplify bit-level operations
enum InterruptFlags : unsigned char
{
	NoInt = 0x0,
	VBlankInt = 0x01,
	LcdStatInt = 0x02,
	TimerInt = 0x04,
	SerialInt = 0x08,
	JoypadInt = 0x10,
	AllInt = 0x1f
};

enum class CpuState
{
	Running,
	Halted,
	Stopped,

	// An unused opcode hard-locked the CPU: it executes nothing more and ignores interrupts until power-off
	Locked
};

// Register file representation for CPU
struct Registers
{
// Allows referencing the full 16-bit register as well as the two 8-bit halves
// Registers stored low-byte first due to x86 endianness
#define REGISTER_PAIR(X, Y) union { struct { unsigned char Y; unsigned char X; }; unsigned short X##Y; }

	REGISTER_PAIR(A, F);
	REGISTER_PAIR(B, C);
	REGISTER_PAIR(D, E);
	REGISTER_PAIR(H, L);

	unsigned short SP;
	unsigned short PC;

	Registers() : AF(0), BC(0), DE(0), HL(0), SP(0xfffe), PC(0)
	{
	}

	bool operator==(const Registers& r1) const
	{
		return r1.AF == AF && r1.BC == BC && r1.DE == DE && r1.HL == HL && r1.SP == SP && r1.PC == PC;
	}
};

// Hardware that runs alongside the CPU, such as the timer. The CPU runs it one machine cycle at a time as each
// instruction executes, so that every memory access sees it as it is at that point in the instruction
class ClockedHardware
{
public:
	virtual ~ClockedHardware() = default;

	// T-cycles (4194304 Hz)
	virtual void RunCycles(int cycles) = 0;

	// T-cycles of time passing while STOP has stopped the system clock. Only hardware with a clock of its own
	// (such as the host's audio output) runs
	virtual void RunStoppedCycles(int cycles) {}
};

// Gameboy LR35902 CPU
class Cpu
{
	// Machine cycle in terms of system clocks
	static const int OneCycle = 4;

	static const unsigned short HiMemBaseAddress = 0xff00;
	static const unsigned short WaitingInterruptsAddress = 0xff0f;
	static const unsigned short EnabledInterruptsAddress = 0xffff;

	static const unsigned char FirstInterruptVector = 0x40;

protected:
	MemoryMap& _memoryMap;
	ClockedHardware* _hardware;
	Registers _registers;

	CpuState _state;
	uint64_t _totalCycles;

	// Cycles run so far by the current instruction
	int _instructionCycles;

	// Set by the HALT bug: the next opcode fetch doesn't increment PC, so the byte after HALT is read twice
	bool _skipNextPCIncrement;

	// Master interrupt enable (IME)
	bool _interruptsEnabled;

	// EI enables interrupts only after the instruction that follows it
	bool _enableInterruptsAfterNext;
	bool _enableInterruptsAfterThis;

	// Interrupt enable/waiting flags. Not typed as InterruptFlags to facilitate bit operations
	unsigned char _enabledInterrupts;
	unsigned char _waitingInterrupts;

	const std::vector<unsigned char(*)(unsigned char& dest, unsigned char src, bool carry)> _aluOps;
	const std::vector<void(*)(unsigned char& dest, unsigned char& flags)> _prefixCbOps;

	// Jump table for all top-level opcodes
	const std::vector<void(Cpu::*)(unsigned char opcode)> _opcodeJumpTable;

	unsigned char GetNextProgramByte() { return ReadByte(_registers.PC++); }

	unsigned char GetByteOperand() { return GetNextProgramByte(); }
	unsigned short GetWordOperand()
	{
		unsigned short low = GetByteOperand();
		return low | GetByteOperand() << 8;
	}

	// PUSH, CALL and RST spend a machine cycle decrementing SP before writing to the stack
	void PushWord(unsigned short word)
	{
		InternalCycle();
		WriteByte(--_registers.SP, word >> 8);
		WriteByte(--_registers.SP, word & 0xff);
	}

	unsigned short PopWord()
	{
		unsigned short word = ReadByte(_registers.SP++);
		return word | ReadByte(_registers.SP++) << 8;
	}

	unsigned short& GetReg16Ref1(unsigned char opcode)
	{
		switch (opcode & 0x30)
		{
		case 0x0:
			return _registers.BC;
		case 0x10:
			return _registers.DE;
		case 0x20:
			return _registers.HL;
		default:
			return _registers.SP;
		}
	}

	unsigned short& GetReg16Ref2(unsigned char opcode)
	{
		switch (opcode & 0x30)
		{
		case 0x0:
			return _registers.BC;
		case 0x10:
			return _registers.DE;
		default:
			return _registers.HL;
		}
	}

	unsigned short& GetReg16Ref3(unsigned char opcode)
	{
		switch (opcode & 0x30)
		{
		case 0x0:
			return _registers.BC;
		case 0x10:
			return _registers.DE;
		case 0x20:
			return _registers.HL;
		default:
			return _registers.AF;
		}
	}

	unsigned short AddSpImm()
	{
		auto offset = GetByteOperand();

		// Offset is treated as unsigned for flag calculation
		_registers.F = ((_registers.SP & 0xff) + offset & 0x100 ? CarryFlag : NoFlags) |
					   ((_registers.SP & 0xf) + (offset & 0xf) & 0x10 ? HalfCarryFlag : NoFlags);

		// ..but signed for applying to register
		return _registers.SP + static_cast<char>(offset);
	}

	unsigned char* const _regRefs1[8]{ &_registers.B, &_registers.D, &_registers.H, nullptr,
							   &_registers.C, &_registers.E, &_registers.L, &_registers.A };

	unsigned char& GetReg8Ref1(unsigned char opcode) const
	{
		return *_regRefs1[((opcode & 0x30) >> 4 | (opcode & 0x8) >> 1)];
	}

	unsigned char* const _regRefs2[8]{ &_registers.B, &_registers.C, &_registers.D, &_registers.E,
							   &_registers.H, &_registers.L, nullptr, &_registers.A };

	unsigned char& GetReg8Ref2(unsigned char opcode) const
	{
		return *_regRefs2[opcode & 0x7];
	}

	unsigned char PendingInterrupts() const { return _waitingInterrupts & _enabledInterrupts; }

	// A button that JOYP selects is held, pulling its input line (P10-P13) low
	bool SelectedButtonHeld() const { return (_memoryMap.ReadByte(MemoryMap::JoypadPort) & 0xf) != 0xf; }

	// Runs one machine cycle of the rest of the hardware. Every memory access takes one, and is made at its end
	void Tick()
	{
		_instructionCycles += OneCycle;
		if (_hardware != nullptr) _hardware->RunCycles(OneCycle);
	}

	// A machine cycle with no memory access
	void InternalCycle() { Tick(); }

	unsigned char ReadByte(unsigned short address)
	{
		Tick();

		switch (address)
		{
		case WaitingInterruptsAddress:
			return _waitingInterrupts | 0xe0;

		case EnabledInterruptsAddress:
			return _enabledInterrupts;

		default:
			return _memoryMap.ReadByte(address);
		}
	}

	void WriteByte(unsigned short address, unsigned char value)
	{
		Tick();

		switch (address)
		{
		case EnabledInterruptsAddress:
			_enabledInterrupts = value & InterruptFlags::AllInt;
			break;

		case WaitingInterruptsAddress:
			_waitingInterrupts = value & InterruptFlags::AllInt;
			break;

		default:
			_memoryMap.WriteByte(address, value);
		}
	}

	// Returns true if condition for conditional instruction is met
	bool ConditionMet(unsigned char opcode) const;

	void DispatchInterrupt();
	void RunHalted();
	void RunStopped();

	void Nop(unsigned char opcode);

	void Ld16RegImm(unsigned char opcode);

	void St8MemRegAcc(unsigned char opcode);

	void Inc16Reg(unsigned char opcode);

	void Dec16Reg(unsigned char opcode);

	void IncDec8RegOrMem(unsigned char opcode);

	void Ld8RegOrMemImm(unsigned char opcode);

	void Rlca(unsigned char opcode);

	void Rla(unsigned char opcode);

	void Rrca(unsigned char opcode);

	void Rra(unsigned char opcode);

	void St16MemSp(unsigned char opcode);

	void Add16RegReg(unsigned char opcode);

	void Ld8AccMem(unsigned char opcode);

	void Stop(unsigned char opcode);

	void Jr(unsigned char opcode);

	void Daa(unsigned char opcode);

	void Cpl(unsigned char opcode);

	void Scf(unsigned char opcode);

	void Ccf(unsigned char opcode);

	void Ld8RegOrMemRegOrMem(unsigned char opcode);

	void Halt(unsigned char opcode);

	void AluOp8AccRegOrMem(unsigned char opcode);

	void Di(unsigned char opcode);

	void Ei(unsigned char opcode);

	void Ret(unsigned char opcode);

	void Push16Reg(unsigned char opcode);

	void Pop16Reg(unsigned char opcode);

	void Jp(unsigned char opcode);

	void Call(unsigned char opcode);

	void AluOp8AccImm(unsigned char opcode);

	void Rst(unsigned char opcode);

	void St8HiMemImmAcc(unsigned char opcode);

	void St8HiMemCAcc(unsigned char opcode);

	void Ld8AccHiMemImm(unsigned char opcode);

	void Ld8AccHiMemC(unsigned char opcode);

	void Add8SpImm(unsigned char opcode);

	void Ld16HlSpImm(unsigned char opcode);

	void JpHl(unsigned char opcode);

	void Ld8AccMemImm(unsigned char opcode);

	void St8MemImmAcc(unsigned char opcode);

	void Ld16SpHl(unsigned char opcode);

	void PrefixCb(unsigned char opcode);

	void InvalidOp(unsigned char opcode);

public:
	explicit Cpu(MemoryMap& memory);

	void SetClockedHardware(ClockedHardware* hardware) { _hardware = hardware; }

	// Executing instructions: not halted, stopped or locked
	bool IsRunning() const { return _state == CpuState::Running; }

	// STOP has stopped the system clock, so the timer, PPU, APU and serial port don't run
	bool IsStopped() const { return _state == CpuState::Stopped; }

	// Gets the total number of elapsed emulated CPU cycles, including while halted or stopped
	uint64_t GetTotalCycles() const { return _totalCycles; }

	// Simulates an IRQ
	void RequestInterrupt(InterruptFlags interruptFlags);

	// Executes the next emulated CPU instruction, dispatches an interrupt, or runs a machine cycle while halted,
	// stopped or locked.
	// Runs the clocked hardware alongside. Returns emulated CPU cycles elapsed
	int DoNextInstruction();
};

