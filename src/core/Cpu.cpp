#include "stdafx.h"
#include "Cpu.h"
#include "MemoryMap.h"
#include <algorithm>

#ifndef _MSC_VER
// Stand-ins for the MSVC intrinsics the rotate instructions use
static unsigned char _rotl8(unsigned char value, unsigned char shift)
{
	shift &= 7;
	return static_cast<unsigned char>(value << shift | value >> (8 - shift) % 8);
}

static unsigned char _rotr8(unsigned char value, unsigned char shift)
{
	shift &= 7;
	return static_cast<unsigned char>(value >> shift | value << (8 - shift) % 8);
}
#endif

Cpu::Cpu(MemoryMap& memory) :
	_memoryMap(memory), _hardware(nullptr), _state(CpuState::Running), _totalCycles(0), _instructionCycles(0),
	_skipNextPCIncrement(false), _interruptsEnabled(true), _enableInterruptsAfterNext(false),
	_enableInterruptsAfterThis(false), _enabledInterrupts(InterruptFlags::NoInt), _waitingInterrupts(InterruptFlags::NoInt),
	_aluOps
	{
		// ADD
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			auto res = dest + src;
			unsigned char flags = res > 0xff ? CarryFlag : NoFlags;
			flags |= (dest & 0xf) + (src & 0xf) > 0xf ? HalfCarryFlag : NoFlags;
			dest = static_cast<unsigned char>(res);

			return static_cast<unsigned char>(flags | (dest == 0 ? ZeroFlag : NoFlags));
		},

		// ADC
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			auto carryOffset = (carry ? 1 : 0);
			auto res = dest + src + carryOffset;
			unsigned char flags = res > 0xff ? CarryFlag : NoFlags;
			flags |= (dest & 0xf) + (src & 0xf) + carryOffset > 0xf ? HalfCarryFlag : NoFlags;
			dest = static_cast<unsigned char>(res);

			return static_cast<unsigned char>(flags | (dest == 0 ? ZeroFlag : NoFlags));
		},

		// SUB
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			auto res = dest - src;
			unsigned char flags = SubFlag | (res < 0 ? CarryFlag : NoFlags);
			flags |= (dest & 0xf) - (src & 0xf) < 0 ? HalfCarryFlag : NoFlags;
			dest = static_cast<unsigned char>(res);

			return static_cast<unsigned char>(flags | (dest == 0 ? ZeroFlag : NoFlags));
		},

		// SBC
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			auto carryOffset = (carry ? 1 : 0);
			auto res = dest - src - carryOffset;
			unsigned char flags = SubFlag | (res < 0 ? CarryFlag : NoFlags);
			flags |= (dest & 0xf) - (src & 0xf) - carryOffset < 0 ? HalfCarryFlag : NoFlags;
			dest = static_cast<unsigned char>(res);

			return static_cast<unsigned char>(flags | (dest == 0 ? ZeroFlag : NoFlags));
		},

		// AND
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			dest &= src;
			return static_cast<unsigned char>(HalfCarryFlag | (dest == 0 ? ZeroFlag : NoFlags));
		},

		// XOR
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			dest ^= src;
			return static_cast<unsigned char>(dest == 0 ? ZeroFlag : NoFlags);
		},

		// OR
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			dest |= src;
			return static_cast<unsigned char>(dest == 0 ? ZeroFlag : NoFlags);
		},

		// CP
		[](unsigned char& dest, unsigned char src, bool carry)
		{
			auto res = dest - src;
			unsigned char flags = SubFlag | (res < 0 ? CarryFlag : NoFlags);
			flags |= (dest & 0xf) - (src & 0xf) < 0 ? HalfCarryFlag : NoFlags;

			return static_cast<unsigned char>(flags | (res == 0 ? ZeroFlag : NoFlags));
		}
	},
	_prefixCbOps
	{
		// RLC
		[](unsigned char& dest, unsigned char& flags)
		{
			dest = _rotl8(dest, 1);
			flags = dest & 0x1 ? CarryFlag : NoFlags;
		},

		// RRC
		[](unsigned char& dest, unsigned char& flags)
		{
			dest = _rotr8(dest, 1);
			flags = dest & 0x80 ? CarryFlag : NoFlags;
		},

		// RL
		[](unsigned char& dest, unsigned char& flags)
		{
			unsigned char tmp = (flags & CarryFlag) >> 4;
			flags = (dest & 0x80) >> 3;
			dest = dest << 1 | tmp;
		},

		// RR
		[](unsigned char& dest, unsigned char& flags)
		{
			unsigned char tmp = (flags & CarryFlag) << 3;
			flags = (dest & 0x1) << 4;
			dest = dest >> 1 | tmp;
		},

		// SLA
		[](unsigned char& dest, unsigned char& flags)
		{
			flags = (dest & 0x80) >> 3;
			dest <<= 1;
		},

		// SRA
		[](unsigned char& dest, unsigned char& flags)
		{
			flags = (dest & 0x1) << 4;
			dest = dest >> 1 | (dest & 0x80);
		},

		// SWAP
		[](unsigned char& dest, unsigned char& flags)
		{
			dest = _rotl8(dest, 4);
			flags = NoFlags;
		},

		// SRL
		[](unsigned char& dest, unsigned char& flags)
		{
			flags = (dest & 0x1) << 4;
			dest >>= 1;
		},

#define BIT_OP_IMP(n) [](unsigned char& dest, unsigned char& flags) {\
						flags = (flags & CarryFlag) | HalfCarryFlag | (dest & (0x1 << n) ? NoFlags : ZeroFlag);\
					  }
		// BIT
		BIT_OP_IMP(0),
		BIT_OP_IMP(1),
		BIT_OP_IMP(2),
		BIT_OP_IMP(3),
		BIT_OP_IMP(4),
		BIT_OP_IMP(5),
		BIT_OP_IMP(6),
		BIT_OP_IMP(7),


#define RES_OP_IMP(n) [](unsigned char& dest, unsigned char& flags) { dest &= ~(0x1 << n); }

		// RES
		RES_OP_IMP(0),
		RES_OP_IMP(1),
		RES_OP_IMP(2),
		RES_OP_IMP(3),
		RES_OP_IMP(4),
		RES_OP_IMP(5),
		RES_OP_IMP(6),
		RES_OP_IMP(7),


#define SET_OP_IMP(n) [](unsigned char& dest, unsigned char& flags) { dest |= 0x1 << n; }

		// SET
		SET_OP_IMP(0),
		SET_OP_IMP(1),
		SET_OP_IMP(2),
		SET_OP_IMP(3),
		SET_OP_IMP(4),
		SET_OP_IMP(5),
		SET_OP_IMP(6),
		SET_OP_IMP(7),
	},
	_opcodeJumpTable
	{
		&Cpu::Nop, &Cpu::Ld16RegImm, &Cpu::St8MemRegAcc, &Cpu::Inc16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Rlca,
		&Cpu::St16MemSp, &Cpu::Add16RegReg, &Cpu::Ld8AccMem, &Cpu::Dec16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Rrca,

		&Cpu::Stop, &Cpu::Ld16RegImm, &Cpu::St8MemRegAcc, &Cpu::Inc16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Rla,
		&Cpu::Jr, &Cpu::Add16RegReg, &Cpu::Ld8AccMem, &Cpu::Dec16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Rra,

		&Cpu::Jr, &Cpu::Ld16RegImm, &Cpu::St8MemRegAcc, &Cpu::Inc16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Daa,
		&Cpu::Jr, &Cpu::Add16RegReg, &Cpu::Ld8AccMem, &Cpu::Dec16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Cpl,

		&Cpu::Jr, &Cpu::Ld16RegImm, &Cpu::St8MemRegAcc, &Cpu::Inc16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Scf,
		&Cpu::Jr, &Cpu::Add16RegReg, &Cpu::Ld8AccMem, &Cpu::Dec16Reg, &Cpu::IncDec8RegOrMem, &Cpu::IncDec8RegOrMem, &Cpu::Ld8RegOrMemImm, &Cpu::Ccf,


		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,
		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,

		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,
		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,

		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,
		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,

		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Halt, &Cpu::Ld8RegOrMemRegOrMem,
		&Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem, &Cpu::Ld8RegOrMemRegOrMem,


		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,
		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,

		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,
		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,

		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,
		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,

		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,
		&Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem, &Cpu::AluOp8AccRegOrMem,


		&Cpu::Ret, &Cpu::Pop16Reg, &Cpu::Jp, &Cpu::Jp, &Cpu::Call, &Cpu::Push16Reg, &Cpu::AluOp8AccImm, &Cpu::Rst,
		&Cpu::Ret, &Cpu::Ret, &Cpu::Jp, &Cpu::PrefixCb, &Cpu::Call, &Cpu::Call, &Cpu::AluOp8AccImm, &Cpu::Rst,

		&Cpu::Ret, &Cpu::Pop16Reg, &Cpu::Jp, &Cpu::InvalidOp, &Cpu::Call, &Cpu::Push16Reg, &Cpu::AluOp8AccImm, &Cpu::Rst,
		&Cpu::Ret, &Cpu::Ret, &Cpu::Jp, &Cpu::InvalidOp, &Cpu::Call, &Cpu::InvalidOp, &Cpu::AluOp8AccImm, &Cpu::Rst,

		&Cpu::St8HiMemImmAcc, &Cpu::Pop16Reg, &Cpu::St8HiMemCAcc, &Cpu::InvalidOp, &Cpu::InvalidOp, &Cpu::Push16Reg, &Cpu::AluOp8AccImm, &Cpu::Rst,
		&Cpu::Add8SpImm, &Cpu::JpHl, &Cpu::St8MemImmAcc, &Cpu::InvalidOp, &Cpu::InvalidOp, &Cpu::InvalidOp, &Cpu::AluOp8AccImm, &Cpu::Rst,

		&Cpu::Ld8AccHiMemImm, &Cpu::Pop16Reg, &Cpu::Ld8AccHiMemC, &Cpu::Di, &Cpu::InvalidOp, &Cpu::Push16Reg, &Cpu::AluOp8AccImm, &Cpu::Rst,
		&Cpu::Ld16HlSpImm, &Cpu::Ld16SpHl, &Cpu::Ld8AccMemImm, &Cpu::Ei, &Cpu::InvalidOp, &Cpu::InvalidOp, &Cpu::AluOp8AccImm, &Cpu::Rst,
	}
{
}

bool Cpu::ConditionMet(unsigned char opcode) const
{
	unsigned char condition = opcode & 0x10 ? CarryFlag : ZeroFlag;
	auto invert = (opcode & 0x8) == 0;

	return ((_registers.F & condition) != 0) ^ invert;
}

// Instruction timings and the machine cycle of each memory access follow https://gbdev.io/gb-opcodes/optables/
// and blargg's instr_timing and mem_timing test ROMs. Opcode and operand fetches, and every read and write, take a
// machine cycle each; any other cycles are internal.

void Cpu::Nop(unsigned char opcode)
{
}

void Cpu::Ld16RegImm(unsigned char opcode)
{
	GetReg16Ref1(opcode) = GetWordOperand();
}

void Cpu::St8MemRegAcc(unsigned char opcode)
{
	auto& ref = GetReg16Ref2(opcode);
	WriteByte(ref, _registers.A);

	switch (opcode & 0x30)
	{
	case 0x20:
		ref++;
		break;
	case 0x30:
		ref--;
		break;
	}
}

void Cpu::Inc16Reg(unsigned char opcode)
{
	GetReg16Ref1(opcode)++;
	InternalCycle();
}

void Cpu::Dec16Reg(unsigned char opcode)
{
	GetReg16Ref1(opcode)--;
	InternalCycle();
}

void Cpu::IncDec8RegOrMem(unsigned char opcode)
{
	auto inc = !(opcode & 1);
	unsigned char flags = _registers.F & CarryFlag;
	unsigned char newData;

	if ((opcode & 0x38) != 0x30)
	{
		auto& reg = GetReg8Ref1(opcode);
		newData = inc ? ++reg : --reg;
	}
	else
	{
		newData = ReadByte(_registers.HL);
		WriteByte(_registers.HL, inc ? ++newData : --newData);
	}

	flags |= newData == 0 ? ZeroFlag : NoFlags;
	flags |= inc ? 0 : SubFlag;
	_registers.F = flags | (inc && (newData & 0xf) == 0 || !inc && (newData & 0xf) == 0xf ? HalfCarryFlag : NoFlags);
}

void Cpu::Ld8RegOrMemImm(unsigned char opcode)
{
	auto operand = GetByteOperand();

	if ((opcode & 0x38) != 0x30)
	{
		GetReg8Ref1(opcode) = operand;
	}
	else
	{
		WriteByte(_registers.HL, operand);
	}
}

void Cpu::Rlca(unsigned char opcode)
{
	auto newAcc = _rotl8(_registers.A, 1);
	_registers.A = newAcc;
	_registers.F = newAcc & 1 ? CarryFlag : NoFlags;
}

void Cpu::Rla(unsigned char opcode)
{
	auto newAcc = _registers.A << 1;
	_registers.A = static_cast<unsigned char>(newAcc | (_registers.F & 0x10) >> 4);
	_registers.F = (newAcc & 0x100) >> 4;
}

void Cpu::Rrca(unsigned char opcode)
{
	auto newVal = _rotr8(_registers.A, 1);
	_registers.A = newVal;
	_registers.F = newVal & 0x80 ? CarryFlag : NoFlags;
}

void Cpu::Rra(unsigned char opcode)
{
	auto newFlags = (_registers.A & 1) << 4;
	_registers.A = static_cast<unsigned char>(_registers.A >> 1 | (_registers.F & 0x10) << 3);
	_registers.F = newFlags;
}

void Cpu::St16MemSp(unsigned char opcode)
{
	auto address = GetWordOperand();
	WriteByte(address++, _registers.SP & 0xff);
	WriteByte(address, _registers.SP >> 8);
}

void Cpu::Add16RegReg(unsigned char opcode)
{
	auto& regRef = GetReg16Ref1(opcode);
	auto res = _registers.HL + regRef;

	unsigned char flags = _registers.F & ZeroFlag | (res & 0x10000 ? CarryFlag : NoFlags);
	flags |= (_registers.HL & 0xfff) + (regRef & 0xfff) & 0x1000 ? HalfCarryFlag : NoFlags;

	_registers.HL = static_cast<unsigned short>(res);
	_registers.F = flags;

	InternalCycle();
}

void Cpu::Ld8AccMem(unsigned char opcode)
{
	auto& ref = GetReg16Ref2(opcode);
	_registers.A = ReadByte(ref);

	switch (opcode & 0x30)
	{
	case 0x20:
		ref++;
		break;
	case 0x30:
		ref--;
		break;
	}
}

void Cpu::Stop(unsigned char opcode)
{
	// Per the "Using the STOP Instruction" diagram in https://gbdev.io/pandocs/Reducing_Power_Consumption.html
	// (the DMG can't switch speed). With an interrupt pending, STOP is a 1-byte opcode and the byte after it runs
	// as the next opcode; otherwise that byte is skipped
	auto twoByteOpcode = !PendingInterrupts();

	if (SelectedButtonHeld())
	{
		// STOP mode would end at once, so the CPU halts instead or, with an interrupt pending, carries on
		if (twoByteOpcode)
		{
			GetByteOperand();
			_state = CpuState::Halted;
		}

		return;
	}

	// The system clock stops, which resets DIV. Skipping the byte after STOP takes no time that anything can see.
	// Simplification: SameBoy also holds DIV in reset for a cycle when IME is clear, which it notes as untrusted
	_memoryMap.WriteByte(MemoryMap::DividerPort, 0);
	_state = CpuState::Stopped;
	if (twoByteOpcode) _registers.PC++;
}

void Cpu::Jr(unsigned char opcode)
{
	auto condition = (opcode & 0x10) != 0 ? CarryFlag : ZeroFlag;
	auto invert = (opcode & 0x8) == 0;
	auto offset = static_cast<char>(GetByteOperand());

	if (opcode == 0x18 || ((_registers.F & condition) != 0) ^ invert)
	{
		_registers.PC += offset;
		InternalCycle();
	}
}

void Cpu::Daa(unsigned char opcode)
{
	int accValue = _registers.A;
	auto flags = _registers.F & SubFlag;

	if (flags)
	{
		if (_registers.F & HalfCarryFlag) accValue = (accValue - 6) & 0xff;
		if (_registers.F & CarryFlag) accValue -= 0x60;
	}
	else
	{
		if (_registers.F & HalfCarryFlag || (accValue & 0xf) > 9) accValue += 0x06;
		if (_registers.F & CarryFlag || accValue > 0x9f) accValue += 0x60;
	}

	// Carry maintained if it was set before DAA was executed
	if (accValue & 0x100 || _registers.F & CarryFlag) flags |= CarryFlag;

	accValue &= 0xff;
	if (accValue == 0) flags |= ZeroFlag;

	_registers.A = accValue;
	_registers.F = flags;
}

void Cpu::Cpl(unsigned char opcode)
{
	_registers.A = ~_registers.A;
	_registers.F |= SubFlag | HalfCarryFlag;
}

void Cpu::Scf(unsigned char opcode)
{
	_registers.F = _registers.F & ZeroFlag | CarryFlag;
}

void Cpu::Ccf(unsigned char opcode)
{
	_registers.F = _registers.F & (ZeroFlag | CarryFlag) ^ CarryFlag;
}

void Cpu::Ld8RegOrMemRegOrMem(unsigned char opcode)
{
	auto val = (opcode & 0x7) != 0x6 ? GetReg8Ref2(opcode) : ReadByte(_registers.HL);

	if ((opcode & 0x78) != 0x70)
	{
		GetReg8Ref1(opcode) = val;
	}
	else
	{
		WriteByte(_registers.HL, val);
	}
}

void Cpu::Halt(unsigned char opcode)
{
	// Per https://gbdev.io/pandocs/halt.html, with IME clear and an interrupt already pending the CPU doesn't
	// halt, and a hardware bug stops the next opcode fetch from incrementing PC
	if (!_interruptsEnabled && PendingInterrupts()) _skipNextPCIncrement = true;
	else _state = CpuState::Halted;
}

void Cpu::AluOp8AccRegOrMem(unsigned char opcode)
{
	auto operand = (opcode & 0x7) != 0x6 ? GetReg8Ref2(opcode) : ReadByte(_registers.HL);
	_registers.F = _aluOps[(opcode & 0x38) >> 3](_registers.A, operand, (_registers.F & CarryFlag) != 0);
}

void Cpu::Di(unsigned char opcode)
{
	_interruptsEnabled = false;

	// Cancels an EI just before
	_enableInterruptsAfterThis = false;
}

void Cpu::Ei(unsigned char opcode)
{
	// With interrupts already enabled EI does nothing, so it can't re-enable them inside a handler dispatched
	// straight after it
	if (!_interruptsEnabled) _enableInterruptsAfterNext = true;
}

void Cpu::Ret(unsigned char opcode)
{
	auto unconditional = (opcode & 0x1) != 0;

	// A conditional return spends a machine cycle checking the condition
	if (!unconditional) InternalCycle();

	if (unconditional || ConditionMet(opcode))
	{
		_registers.PC = PopWord();
		InternalCycle();

		// RETI enables interrupts straight away, unlike EI
		if (opcode == 0xd9) _interruptsEnabled = true;
	}
}

void Cpu::Push16Reg(unsigned char opcode)
{
	PushWord(GetReg16Ref3(opcode));
}

void Cpu::Pop16Reg(unsigned char opcode)
{
	GetReg16Ref3(opcode) = PopWord();

	// Ensure lower nibble of F is zero after possible pop into it
	_registers.F &= 0xf0;
}

void Cpu::Jp(unsigned char opcode)
{
	auto address = GetWordOperand();

	if (opcode == 0xc3 || ConditionMet(opcode))
	{
		_registers.PC = address;
		InternalCycle();
	}
}

void Cpu::Call(unsigned char opcode)
{
	auto address = GetWordOperand();

	if (opcode == 0xcd || ConditionMet(opcode))
	{
		PushWord(_registers.PC);
		_registers.PC = address;
	}
}

void Cpu::AluOp8AccImm(unsigned char opcode)
{
	auto operand = GetByteOperand();
	_registers.F = _aluOps[(opcode & 0x38) >> 3](_registers.A, operand, (_registers.F & CarryFlag) != 0);
}

void Cpu::Rst(unsigned char opcode)
{
	PushWord(_registers.PC);
	_registers.PC = opcode - 0xc7;
}

void Cpu::St8HiMemImmAcc(unsigned char opcode)
{
	WriteByte(HiMemBaseAddress + GetByteOperand(), _registers.A);
}

void Cpu::St8HiMemCAcc(unsigned char opcode)
{
	WriteByte(HiMemBaseAddress + _registers.C, _registers.A);
}

void Cpu::Ld8AccHiMemImm(unsigned char opcode)
{
	_registers.A = ReadByte(HiMemBaseAddress + GetByteOperand());
}

void Cpu::Ld8AccHiMemC(unsigned char opcode)
{
	_registers.A = ReadByte(HiMemBaseAddress + _registers.C);
}

void Cpu::Add8SpImm(unsigned char opcode)
{
	_registers.SP = AddSpImm();
	InternalCycle();
	InternalCycle();
}

void Cpu::Ld16HlSpImm(unsigned char opcode)
{
	_registers.HL = AddSpImm();
	InternalCycle();
}

void Cpu::JpHl(unsigned char opcode)
{
	_registers.PC = _registers.HL;
}

void Cpu::Ld8AccMemImm(unsigned char opcode)
{
	_registers.A = ReadByte(GetWordOperand());
}

void Cpu::St8MemImmAcc(unsigned char opcode)
{
	WriteByte(GetWordOperand(), _registers.A);
}

void Cpu::Ld16SpHl(unsigned char opcode)
{
	_registers.SP = _registers.HL;
	InternalCycle();
}

void Cpu::PrefixCb(unsigned char opcode)
{
	auto nextOpcode = GetNextProgramByte();
	auto isReg = (nextOpcode & 0x7) != 0x6;
	auto val = isReg ? GetReg8Ref2(nextOpcode) : ReadByte(_registers.HL);

	_prefixCbOps[nextOpcode >> 3](val, _registers.F);

	// CB-prefixed opcodes >= 0x40 either don't update the zero flag, or
	// else the/ op-specific logic called above already updates it
	if (nextOpcode < 0x40)
	{
		_registers.F = _registers.F & ~ZeroFlag | (val == 0 ? ZeroFlag : NoFlags);
	}

	// Opcodes within this range (BIT) don't write a result (other than flags)
	if (nextOpcode < 0x40 || nextOpcode > 0x7f)
	{
		if (isReg)
		{
			GetReg8Ref2(nextOpcode) = val;
		}
		else
		{
			WriteByte(_registers.HL, val);
		}
	}
}

void Cpu::InvalidOp(unsigned char opcode)
{
	// Per https://gbdev.io/pandocs/CPU_Instruction_Set.html, the unused opcodes hard-lock the CPU
	_state = CpuState::Locked;
}

void Cpu::RequestInterrupt(InterruptFlags interruptFlags)
{
	_waitingInterrupts |= interruptFlags;
}

void Cpu::DispatchInterrupt()
{
	// Per https://gbdev.io/pandocs/Interrupts.html#interrupt-handling, dispatch takes five machine cycles: two
	// wait states, two to push PC and one to jump to the handler. The first wait state is the opcode fetch the
	// dispatch replaces, which DoNextInstruction has already made. An EI still waiting to take effect (the first
	// of EI; EI) is dropped, so the handler runs with interrupts disabled
	_interruptsEnabled = false;
	_enableInterruptsAfterNext = false;
	InternalCycle();

	// The opcode fetch that the dispatch replaced didn't increment PC if the HALT bug struck, so the handler
	// returns to the HALT
	if (_skipNextPCIncrement)
	{
		_skipNextPCIncrement = false;
		_registers.PC--;
	}

	WriteByte(--_registers.SP, _registers.PC >> 8);

	// The interrupt is chosen only after the high byte of PC is pushed. If that push overwrote IE so that no
	// interrupt is still pending, the CPU jumps to 0
	auto pendingInterrupts = PendingInterrupts();
	WriteByte(--_registers.SP, _registers.PC & 0xff);
	InternalCycle();

	_registers.PC = 0;

	for (auto i = 0; i < 5; i++)
	{
		if (pendingInterrupts & 1 << i)
		{
			_waitingInterrupts &= ~(1 << i);
			_registers.PC = FirstInterruptVector + i * 8;
			break;
		}
	}
}

void Cpu::RunHalted()
{
	if (!PendingInterrupts())
	{
		InternalCycle();
		return;
	}

	// A pending interrupt ends the halt, whether or not interrupts are enabled, and waking takes a machine cycle
	_state = CpuState::Running;
	InternalCycle();
}

void Cpu::RunStopped()
{
	// Time passes, but nothing runs on the system clock
	_instructionCycles += OneCycle;
	if (_hardware != nullptr) _hardware->RunStoppedCycles(OneCycle);

	// STOP mode ends when a selected button pulls its input line low, whether or not the joypad interrupt is enabled.
	// Simplification: waking takes no time here, where SameBoy spends 2 machine cycles
	if (SelectedButtonHeld()) _state = CpuState::Running;
}

int Cpu::DoNextInstruction()
{
	_instructionCycles = 0;

	if (_state == CpuState::Halted) RunHalted();

	if (_state == CpuState::Running)
	{
		// The CPU checks for interrupts as it fetches the next opcode, so it sees one requested during the last
		// machine cycle of the previous instruction. A dispatch discards the opcode and leaves PC unchanged
		auto opcode = ReadByte(_registers.PC);

		if (_interruptsEnabled && PendingInterrupts())
		{
			DispatchInterrupt();
		}
		else
		{
			if (!_skipNextPCIncrement) ++_registers.PC;
			_skipNextPCIncrement = false;

			// An EI before this instruction takes effect once it completes, unless this instruction is DI
			_enableInterruptsAfterThis = _enableInterruptsAfterNext;
			_enableInterruptsAfterNext = false;

			(this->*_opcodeJumpTable[opcode])(opcode);

			if (_enableInterruptsAfterThis) _interruptsEnabled = true;
		}
	}
	else if (_state == CpuState::Stopped)
	{
		RunStopped();
	}
	else if (_state == CpuState::Locked)
	{
		// The rest of the hardware keeps running
		InternalCycle();
	}

	_totalCycles += _instructionCycles;
	return _instructionCycles;
}
