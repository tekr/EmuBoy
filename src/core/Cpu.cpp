#include "stdafx.h"
#include "Cpu.h"
#include "MemoryMap.h"
#include <iostream>
#include <algorithm>

Cpu::Cpu(MemoryMap& memory) :
	_memoryMap(memory), _state(CpuState::Running), _totalCycles(0), _interruptsEnabled(true),
	_enabledInterrupts(InterruptFlags::None), _waitingInterrupts(InterruptFlags::None), _interruptCheckRequired(false),
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

Cpu::~Cpu()
{
}

bool Cpu::ConditionMet(unsigned char opcode) const
{
	unsigned char condition = opcode & 0x10 ? CarryFlag : ZeroFlag;
	auto invert = (opcode & 0x8) == 0;

	return ((_registers.F & condition) != 0) ^ invert;
}

bool Cpu::InterruptTriggered()
{
	auto pendingInterrupts = _waitingInterrupts & _enabledInterrupts;
	auto interruptTriggered = _interruptsEnabled && pendingInterrupts;

	if (interruptTriggered)
	{
		_interruptsEnabled = false;

		auto entry = std::find_if(_intVectors.begin(), _intVectors.end(), [pendingInterrupts]
					(const std::pair<InterruptFlags, unsigned char>& e){ return pendingInterrupts & e.first; });

		_waitingInterrupts ^= entry->first;

		PushWord(_registers.PC);
		_registers.PC = entry->second;
	}

	return interruptTriggered;
}

int Cpu::Nop(unsigned char opcode)
{
	return OneCycle;
}

int Cpu::Ld16RegImm(unsigned char opcode)
{
	GetReg16Ref1(opcode) = GetWordOperand();
	return ThreeCycles;
}

int Cpu::St8MemRegAcc(unsigned char opcode)
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

	return TwoCycles;
}

int Cpu::Inc16Reg(unsigned char opcode)
{
	GetReg16Ref1(opcode)++;
	return TwoCycles;
}

int Cpu::Dec16Reg(unsigned char opcode)
{
	GetReg16Ref1(opcode)--;
	return TwoCycles;
}

int Cpu::IncDec8RegOrMem(unsigned char opcode)
{
	auto inc = !(opcode & 1);
	auto cycleCount = OneCycle;
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
		cycleCount = ThreeCycles;
	}

	flags |= newData == 0 ? ZeroFlag : NoFlags;
	flags |= inc ? 0 : SubFlag;
	_registers.F = flags | (inc && (newData & 0xf) == 0 || !inc && (newData & 0xf) == 0xf ? HalfCarryFlag : NoFlags);

	return cycleCount;
}

int Cpu::Ld8RegOrMemImm(unsigned char opcode)
{
	auto operand = GetByteOperand();

	if ((opcode & 0x38) != 0x30)
	{
		GetReg8Ref1(opcode) = operand;
		return TwoCycles;
	}

	WriteByte(_registers.HL, operand);
	return ThreeCycles;
}

int Cpu::Rlca(unsigned char opcode)
{
	auto newAcc = _rotl8(_registers.A, 1);
	_registers.A = newAcc;
	_registers.F = newAcc & 1 ? CarryFlag : NoFlags;
	return OneCycle;
}

int Cpu::Rla(unsigned char opcode)
{
	auto newAcc = _registers.A << 1;
	_registers.A = static_cast<unsigned char>(newAcc | (_registers.F & 0x10) >> 4);
	_registers.F = (newAcc & 0x100) >> 4;
	return OneCycle;
}

int Cpu::Rrca(unsigned char opcode)
{
	auto newVal = _rotr8(_registers.A, 1);
	_registers.A = newVal;
	_registers.F = newVal & 0x80 ? CarryFlag : NoFlags;
	return OneCycle;
}

int Cpu::Rra(unsigned char opcode)
{
	auto newFlags = (_registers.A & 1) << 4;
	_registers.A = static_cast<unsigned char>(_registers.A >> 1 | (_registers.F & 0x10) << 3);
	_registers.F = newFlags;
	return OneCycle;
}

int Cpu::St16MemSp(unsigned char opcode)
{
	auto address = GetWordOperand();
	WriteByte(address++, _registers.SP & 0xff);
	WriteByte(address, _registers.SP >> 8);
	return FiveCycles;
}

int Cpu::Add16RegReg(unsigned char opcode)
{
	auto& regRef = GetReg16Ref1(opcode);
	auto res = _registers.HL + regRef;

	unsigned char flags = _registers.F & ZeroFlag | (res & 0x10000 ? CarryFlag : NoFlags);
	flags |= (_registers.HL & 0xfff) + (regRef & 0xfff) & 0x1000 ? HalfCarryFlag : NoFlags;

	_registers.HL = static_cast<unsigned short>(res);
	_registers.F = flags;

	return TwoCycles;
}

int Cpu::Ld8AccMem(unsigned char opcode)
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

	return TwoCycles;
}

int Cpu::Stop(unsigned char opcode)
{
	_state = CpuState::Stopped;
	return OneCycle;
}

int Cpu::Jr(unsigned char opcode)
{
	auto condition = (opcode & 0x10) != 0 ? CarryFlag : ZeroFlag;
	auto invert = (opcode & 0x8) == 0;
	auto offset = static_cast<char>(GetByteOperand());
	auto cycleCount = TwoCycles;

	if (opcode == 0x18 || ((_registers.F & condition) != 0) ^ invert)
	{
		_registers.PC += offset;
		cycleCount = ThreeCycles;
	}

	return cycleCount;
}

int Cpu::Daa(unsigned char opcode)
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

	return OneCycle;
}

int Cpu::Cpl(unsigned char opcode)
{
	_registers.A = ~_registers.A;
	_registers.F |= SubFlag | HalfCarryFlag;

	return OneCycle;
}

int Cpu::Scf(unsigned char opcode)
{
	_registers.F = _registers.F & ZeroFlag | CarryFlag;
	return OneCycle;
}

int Cpu::Ccf(unsigned char opcode)
{
	_registers.F = _registers.F & (ZeroFlag | CarryFlag) ^ CarryFlag;
	return OneCycle;
}

int Cpu::Ld8RegOrMemRegOrMem(unsigned char opcode)
{
	unsigned char val;
	auto cycleCount = OneCycle;

	if ((opcode & 0x7) != 0x6)
	{
		val = GetReg8Ref2(opcode);
	}
	else
	{
		val = ReadByte(_registers.HL);
		cycleCount = TwoCycles;
	}

	if ((opcode & 0x78) != 0x70)
	{
		GetReg8Ref1(opcode) = val;
	}
	else
	{
		WriteByte(_registers.HL, val);
		cycleCount = TwoCycles;
	}

	return cycleCount;
}

int Cpu::Halt(unsigned char opcode)
{
	_state = CpuState::Halted;
	return OneCycle;
}

int Cpu::AluOp8AccRegOrMem(unsigned char opcode)
{
	auto cycleCount = OneCycle;
	unsigned char operand1;

	if ((opcode & 0x7) != 0x6)
	{
		operand1 = GetReg8Ref2(opcode);
	}
	else
	{
		operand1 = ReadByte(_registers.HL);
		cycleCount = TwoCycles;
	}

	_registers.F = _aluOps[(opcode & 0x38) >> 3](_registers.A, operand1, (_registers.F & CarryFlag) != 0);

	return cycleCount;
}

int Cpu::Di(unsigned char opcode)
{
	// TODO: GB CPU disables interrupts after the instruction following this one
	_interruptsEnabled = false;

	return OneCycle;
}

int Cpu::Ei(unsigned char opcode)
{
	// TODO: GB CPU enables interrupts after the instruction following this one
	_interruptsEnabled = true;
	_interruptCheckRequired = true;

	return OneCycle;
}

int Cpu::Ret(unsigned char opcode)
{
	auto cycleCount = TwoCycles;

	if ((opcode & 0x1) != 0 || ConditionMet(opcode))
	{
		_registers.PC = PopWord();
		cycleCount = (opcode & 0x1) != 0 ? FourCycles : FiveCycles;

		if (opcode == 0xd9) // RETI
		{
			Ei(opcode);
		}
	}

	return cycleCount;
}

int Cpu::Push16Reg(unsigned char opcode)
{
	PushWord(GetReg16Ref3(opcode));
	return FourCycles;
}

int Cpu::Pop16Reg(unsigned char opcode)
{
	GetReg16Ref3(opcode) = PopWord();

	// Ensure lower nibble of F is zero after possible pop into it
	_registers.F &= 0xf0;

	return ThreeCycles;
}

int Cpu::Jp(unsigned char opcode)
{
	auto address = GetWordOperand();
	auto cycleCount = ThreeCycles;

	if (opcode == 0xc3 || ConditionMet(opcode))
	{
		_registers.PC = address;
		cycleCount = FourCycles;
	}

	return cycleCount;
}

int Cpu::Call(unsigned char opcode)
{
	auto address = GetWordOperand();
	auto cycleCount = ThreeCycles;

	if (opcode == 0xcd || ConditionMet(opcode))
	{
		PushWord(_registers.PC);
		_registers.PC = address;
		cycleCount = SixCycles;
	}

	return cycleCount;
}

int Cpu::AluOp8AccImm(unsigned char opcode)
{
	auto operand = GetByteOperand();
	_registers.F = _aluOps[(opcode & 0x38) >> 3](_registers.A, operand, (_registers.F & CarryFlag) != 0);

	return TwoCycles;
}

int Cpu::Rst(unsigned char opcode)
{
	PushWord(_registers.PC);
	_registers.PC = opcode - 0xc7;

	return FourCycles;
}

int Cpu::St8HiMemImmAcc(unsigned char opcode)
{
	WriteByte(HiMemBaseAddress + GetByteOperand(), _registers.A);
	return ThreeCycles;
}

int Cpu::St8HiMemCAcc(unsigned char opcode)
{
	WriteByte(HiMemBaseAddress + _registers.C, _registers.A);
	return TwoCycles;
}

int Cpu::Ld8AccHiMemImm(unsigned char opcode)
{
	_registers.A = ReadByte(HiMemBaseAddress + GetByteOperand());
	return ThreeCycles;
}

int Cpu::Ld8AccHiMemC(unsigned char opcode)
{
	_registers.A = ReadByte(HiMemBaseAddress + _registers.C);
	return ThreeCycles;
}

int Cpu::Add8SpImm(unsigned char opcode)
{
	_registers.SP = AddSpImm();
	return FourCycles;
}

int Cpu::Ld16HlSpImm(unsigned char opcode)
{
	_registers.HL = AddSpImm();
	return ThreeCycles;
}

int Cpu::JpHl(unsigned char opcode)
{
	_registers.PC = _registers.HL;
	return OneCycle;
}

int Cpu::Ld8AccMemImm(unsigned char opcode)
{
	_registers.A = ReadByte(GetWordOperand());
	return FourCycles;
}

int Cpu::St8MemImmAcc(unsigned char opcode)
{
	WriteByte(GetWordOperand(), _registers.A);
	return FourCycles;
}

int Cpu::Ld16SpHl(unsigned char opcode)
{
	_registers.SP = _registers.HL;
	return TwoCycles;
}

int Cpu::PrefixCb(unsigned char opcode)
{
	auto nextOpcode = GetNextProgramByte();

	unsigned char val;
	auto cycleCount = TwoCycles;
	auto isReg = (nextOpcode & 0x7) != 0x6;

	if (isReg)
	{
		val = GetReg8Ref2(nextOpcode);
	}
	else
	{
		val = ReadByte(_registers.HL);
		cycleCount = FourCycles;
	}

	_prefixCbOps[nextOpcode >> 3](val, _registers.F);

	// CB-prefixed opcodes >= 0x40 either don't update the zero flag, or
	// else the/ op-specific logic called above already updates it
	if (nextOpcode < 0x40)
	{
		_registers.F = _registers.F & ~ZeroFlag | (val == 0 ? ZeroFlag : NoFlags);
	}

	if (isReg)
	{
		GetReg8Ref2(nextOpcode) = val;
	}
	else
	{
		WriteByte(_registers.HL, val);
	}

	return cycleCount;
}

int Cpu::InvalidOp(unsigned char opcode)
{
	std::cout << "Invalid opcode " << std::hex << opcode << " encountered" << std::endl;
	throw std::exception("Invalid operation");
}

void Cpu::RequestInterrupt(InterruptFlags interruptFlags)
{
	_waitingInterrupts |= interruptFlags;

	// Master interrupt enable is not checked when waking from halt
	if (_state == CpuState::Halted && _waitingInterrupts & _enabledInterrupts)
	{
		_state = CpuState::Running;

		// Hardware bug causes next PC increment to be skipped if
		// master interrupt enabled was false
		_skipNextPCIncrement = !_interruptsEnabled;
	}

	_interruptCheckRequired = true;
}

int Cpu::DoNextInstruction()
{
	StartIfRequired();
	if (_state != CpuState::Running) return 0;

	if (_interruptCheckRequired)
	{
		// Can't find interrupt ack time in docs, so assuming it's the same as the
		// RST instruction (difference being an interrupt also updates the master
		// interrupt enable and waiting interrupt flag)
		if (InterruptTriggered()) return FourCycles;

		_interruptCheckRequired = false;
	}

	auto opcode = GetNextProgramByte();

	auto opPtr = _opcodeJumpTable[opcode];
	auto cycles = (this ->* opPtr)(opcode);

	_totalCycles += cycles;
	return cycles;
}
