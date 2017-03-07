#pragma once
#include "MemoryMap.h"
#include <vector>

// Avoids enum class to simplify bit-level operations
enum CpuFlags : uint8_t
{
	ZeroFlag = 0x80,
	SubFlag = 0x40,
	HalfCarryFlag = 0x20,
	CarryFlag = 0x10,
	NoFlags = 0x00,
	AllFlags = 0xff
};

// Avoids enum class to simplify bit-level operations
enum InterruptFlags : uint8_t
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
#define REGISTER_PAIR(X, Y) union { struct { uint8_t Y; uint8_t X; }; uint16_t X##Y; }

	REGISTER_PAIR(A, F);
	REGISTER_PAIR(B, C);
	REGISTER_PAIR(D, E);
	REGISTER_PAIR(H, L);

	uint16_t SP;
	uint16_t PC;

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

	static const uint16_t HiMemBaseAddress = 0xff00;
	static const uint16_t WaitingInterruptsAddress = 0xff0f;
	static const uint16_t EnabledInterruptsAddress = 0xffff;

	const std::vector<std::pair<InterruptFlags, uint8_t>> _intVectors
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
	uint8_t _enabledInterrupts;
	uint8_t _waitingInterrupts;

	bool _interruptCheckRequired;

	const std::vector<uint8_t(*)(uint8_t& dest, uint8_t src, bool carry)> _aluOps;
	const std::vector<void(*)(uint8_t& dest, uint8_t& flags)> _prefixCbOps;

	// Jump table for all top-level opcodes
	const std::vector<int(Cpu::*)(uint8_t opcode)> _opcodeJumpTable;

	uint8_t GetNextProgramByte()
	{
		auto result = ReadByte(_registers.PC);

		if (!_skipNextPCIncrement) ++_registers.PC;
		_skipNextPCIncrement = false;

		return result;
	}

	uint8_t GetByteOperand() { return GetNextProgramByte(); }
	uint16_t GetWordOperand() { return GetByteOperand() | GetByteOperand() << 8; }

	void PushWord(uint16_t word)
	{
		WriteByte(--_registers.SP, word >> 8);
		WriteByte(--_registers.SP, word & 0xff);
	}

	uint16_t PopWord()
	{
		uint16_t word = ReadByte(_registers.SP++);
		return word | ReadByte(_registers.SP++) << 8;
	}

	uint16_t& GetReg16Ref1(uint8_t opcode)
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

	uint16_t& GetReg16Ref2(uint8_t opcode)
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

	uint16_t& GetReg16Ref3(uint8_t opcode)
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

	uint16_t Cpu::AddSpImm()
	{
		auto offset = GetByteOperand();

		// Offset is treated as unsigned for flag calculation
		_registers.F = ((_registers.SP & 0xff) + offset & 0x100 ? CarryFlag : NoFlags) |
					   ((_registers.SP & 0xf) + (offset & 0xf) & 0x10 ? HalfCarryFlag : NoFlags);

		// ..but signed for applying to register
		return _registers.SP + static_cast<int8_t>(offset);
	}

	uint8_t* const _regRefs1[8]{ &_registers.B, &_registers.D, &_registers.H, nullptr,
							   &_registers.C, &_registers.E, &_registers.L, &_registers.A };

	uint8_t& GetReg8Ref1(uint8_t opcode) const
	{
		return *_regRefs1[((opcode & 0x30) >> 4 | (opcode & 0x8) >> 1)];
	}

	uint8_t* const _regRefs2[8]{ &_registers.B, &_registers.C, &_registers.D, &_registers.E,
							   &_registers.H, &_registers.L, nullptr, &_registers.A };

	uint8_t& GetReg8Ref2(uint8_t opcode) const
	{
		return *_regRefs2[opcode & 0x7];
	}

	uint8_t ReadByte(uint16_t address) const
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

	void WriteByte(uint16_t address, uint8_t value)
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

	// Returns true if condition for conditional instruction is met
	bool ConditionMet(uint8_t opcode) const;

	bool InterruptTriggered();

	int Nop(uint8_t opcode);

	int Ld16RegImm(uint8_t opcode);

	int St8MemRegAcc(uint8_t opcode);

	int Inc16Reg(uint8_t opcode);

	int Dec16Reg(uint8_t opcode);

	int IncDec8RegOrMem(uint8_t opcode);

	int Ld8RegOrMemImm(uint8_t opcode);

	int Rlca(uint8_t opcode);

	int Rla(uint8_t opcode);

	int Rrca(uint8_t opcode);

	int Rra(uint8_t opcode);

	int St16MemSp(uint8_t opcode);

	int Add16RegReg(uint8_t opcode);

	int Ld8AccMem(uint8_t opcode);

	int Stop(uint8_t opcode);

	int Jr(uint8_t opcode);

	int Daa(uint8_t opcode);

	int Cpl(uint8_t opcode);

	int Scf(uint8_t opcode);

	int Ccf(uint8_t opcode);

	int Ld8RegOrMemRegOrMem(uint8_t opcode);

	int Halt(uint8_t opcode);

	int AluOp8AccRegOrMem(uint8_t opcode);

	int Di(uint8_t opcode);

	int Ei(uint8_t opcode);

	int Ret(uint8_t opcode);

	int Push16Reg(uint8_t opcode);

	int Pop16Reg(uint8_t opcode);

	int Jp(uint8_t opcode);

	int Call(uint8_t opcode);

	int AluOp8AccImm(uint8_t opcode);

	int Rst(uint8_t opcode);

	int St8HiMemImmAcc(uint8_t opcode);

	int St8HiMemCAcc(uint8_t opcode);

	int Ld8AccHiMemImm(uint8_t opcode);

	int Ld8AccHiMemC(uint8_t opcode);

	int Add8SpImm(uint8_t opcode);

	int Ld16HlSpImm(uint8_t opcode);

	int JpHl(uint8_t opcode);

	int Ld8AccMemImm(uint8_t opcode);

	int St8MemImmAcc(uint8_t opcode);

	int Ld16SpHl(uint8_t opcode);

	int PrefixCb(uint8_t opcode);

	int InvalidOp(uint8_t opcode);

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

