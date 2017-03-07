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
	Stopped
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

// Gameboy LR35902 CPU
class Cpu
{
	//Machine cycles in terms of system clocks
	static const int OneCycle	 = 4;
	static const int TwoCycles	 = OneCycle * 2;
	static const int ThreeCycles = OneCycle * 3;
	static const int FourCycles  = OneCycle * 4;
	static const int FiveCycles  = OneCycle * 5;
	static const int SixCycles   = OneCycle * 6;

	static const unsigned short HiMemBaseAddress = 0xff00;
	static const unsigned short WaitingInterruptsAddress = 0xff0f;
	static const unsigned short EnabledInterruptsAddress = 0xffff;

	const std::vector<std::pair<InterruptFlags, unsigned char>> _intVectors
	{
		{ InterruptFlags::VBlankInt,  0x40 },
		{ InterruptFlags::LcdStatInt, 0x48 },
		{ InterruptFlags::TimerInt,   0x50 },
		{ InterruptFlags::SerialInt,  0x58 },
		{ InterruptFlags::JoypadInt,  0x60 },
	};

protected:
	MemoryMap& _memoryMap;
	Registers _registers;

	CpuState _state;
	uint64_t _totalCycles;
	int _extraCyclesConsumed;

	bool _skipNextPCIncrement;

	// Master interrupt enable
	bool _interruptsEnabled;

	// Interrupt enable/waiting flags. Not typed as InterruptFlags to facilitate bit operations
	unsigned char _enabledInterrupts;
	unsigned char _waitingInterrupts;

	bool _interruptCheckRequired;

	const std::vector<unsigned char(*)(unsigned char& dest, unsigned char src, bool carry)> _aluOps;
	const std::vector<void(*)(unsigned char& dest, unsigned char& flags)> _prefixCbOps;

	// Jump table for all top-level opcodes
	const std::vector<int(Cpu::*)(unsigned char opcode)> _opcodeJumpTable;

	unsigned char GetNextProgramByte()
	{
		auto result = ReadByte(_registers.PC);

		if (!_skipNextPCIncrement) ++_registers.PC;
		_skipNextPCIncrement = false;

		return result;
	}

	unsigned char GetByteOperand() { return GetNextProgramByte(); }
	unsigned short GetWordOperand() { return GetByteOperand() | GetByteOperand() << 8; }

	void PushWord(unsigned short word)
	{
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

	unsigned char ReadByte(unsigned short address) const
	{
		switch (address)
		{
		case WaitingInterruptsAddress:
			return _waitingInterrupts | 0xe0;

		case EnabledInterruptsAddress:
			return _enabledInterrupts ;

		default:
			return _memoryMap.ReadByte(address);
		}
	}

	void WriteByte(unsigned short address, unsigned char value)
	{
		// TODO: Not great that every memory write pays the cost of checking whether interrupt
		// registers are being updated. Could move this to the memory map address decoder and
		// have it notify the CPU to check interrupts on the next instruction
		switch (address)
		{
		case EnabledInterruptsAddress:
			_enabledInterrupts = value & InterruptFlags::AllInt;
			_interruptCheckRequired = true;
			break;

		case WaitingInterruptsAddress:
			_waitingInterrupts = value & InterruptFlags::AllInt;
			_interruptCheckRequired = true;
			break;

		default:
			_memoryMap.WriteByte(address, value);
		}
	}

	static uint8_t rotl8(uint8_t num, unsigned int n)
	{
		const unsigned int mask = 7;

		// Avoid undefined behaviour
		n &= mask;

#pragma warning(push)
#pragma warning(disable:4146)
		// Unary minus on unsigned -> unsigned is intentional here
		return num << n | num >> (-n & mask);
#pragma warning(pop)
	}

	static uint8_t rotr8(uint8_t num, unsigned int n)
	{
		return rotl8(num, 8 - n);
	}

	// Returns true if condition for conditional instruction is met
	bool ConditionMet(unsigned char opcode) const;

	bool InterruptTriggered();

	int Nop(unsigned char opcode);

	int Ld16RegImm(unsigned char opcode);

	int St8MemRegAcc(unsigned char opcode);

	int Inc16Reg(unsigned char opcode);

	int Dec16Reg(unsigned char opcode);

	int IncDec8RegOrMem(unsigned char opcode);

	int Ld8RegOrMemImm(unsigned char opcode);

	int Rlca(unsigned char opcode);

	int Rla(unsigned char opcode);

	int Rrca(unsigned char opcode);

	int Rra(unsigned char opcode);

	int St16MemSp(unsigned char opcode);

	int Add16RegReg(unsigned char opcode);

	int Ld8AccMem(unsigned char opcode);

	int Stop(unsigned char opcode);

	int Jr(unsigned char opcode);

	int Daa(unsigned char opcode);

	int Cpl(unsigned char opcode);

	int Scf(unsigned char opcode);

	int Ccf(unsigned char opcode);

	int Ld8RegOrMemRegOrMem(unsigned char opcode);

	int Halt(unsigned char opcode);

	int AluOp8AccRegOrMem(unsigned char opcode);

	int Di(unsigned char opcode);

	int Ei(unsigned char opcode);

	int Ret(unsigned char opcode);

	int Push16Reg(unsigned char opcode);

	int Pop16Reg(unsigned char opcode);

	int Jp(unsigned char opcode);

	int Call(unsigned char opcode);

	int AluOp8AccImm(unsigned char opcode);

	int Rst(unsigned char opcode);

	int St8HiMemImmAcc(unsigned char opcode);

	int St8HiMemCAcc(unsigned char opcode);

	int Ld8AccHiMemImm(unsigned char opcode);

	int Ld8AccHiMemC(unsigned char opcode);

	int Add8SpImm(unsigned char opcode);

	int Ld16HlSpImm(unsigned char opcode);

	int JpHl(unsigned char opcode);

	int Ld8AccMemImm(unsigned char opcode);

	int St8MemImmAcc(unsigned char opcode);

	int Ld16SpHl(unsigned char opcode);

	int PrefixCb(unsigned char opcode);

	int InvalidOp(unsigned char opcode);

public:
	explicit Cpu(MemoryMap& memory);

	bool IsClockRunning() const	{ return _state == CpuState::Running; }

	// Gets the total number of elapsed emulated CPU cycles (does not count when CPU is stopped/halted)
	uint64_t GetTotalCycles() const { return _totalCycles; }

	// Simulates an IRQ
	void RequestInterrupt(InterruptFlags interruptFlags);

	// Executes the next emulated CPU instruction. Returns emulated CPU cycles elapsed
	int DoNextInstruction();
};

