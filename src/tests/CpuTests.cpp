
#include <functional>
#include <gtest/gtest.h>
#include "CpuTestFixture.h"

const int OneCycle = 4;
const int TwoCycles = OneCycle * 2;
const int ThreeCycles = OneCycle * 3;
const int FourCycles = OneCycle * 4;
const int FiveCycles = OneCycle * 5;
const int SixCycles = OneCycle * 6;

typedef std::function<unsigned char&(Registers& registers, TestMemoryMap& tm)> RegMem8Selector;
typedef std::function<unsigned short&(Registers& registers)> Reg16Selector;

struct TestData8Reg
{
	RegMem8Selector Selector;
	unsigned char IncOpcode;
	unsigned char DecOpcode;
	int IncDecCycleCount;
	unsigned char LdImmOpcode;
	int LdCycleCount;
};

const std::vector<TestData8Reg> Reg8TestCases
{
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.B; },	  0x04, 0x05, OneCycle,	   0x06, TwoCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.D; },	  0x14, 0x15, OneCycle,    0x16, TwoCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.H; },	  0x24, 0x25, OneCycle,    0x26, TwoCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return tm[r.HL]; }, 0x34, 0x35, ThreeCycles, 0x36, ThreeCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.C; },	  0x0c, 0x0d, OneCycle,    0x0e, TwoCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.E; },	  0x1c, 0x1d, OneCycle,    0x1e, TwoCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.L; },	  0x2c, 0x2d, OneCycle,    0x2e, TwoCycles },
	{ [](Registers& r, TestMemoryMap& tm) -> auto& { return r.A; },	  0x3c, 0x3d, OneCycle,    0x3e, TwoCycles }
};

struct TestData16Reg1
{
	Reg16Selector Selector;
	unsigned char LdImmOpcode;
	unsigned char IncOpcode;
	unsigned char DecOpcode;
	unsigned char AddHlOpcode;
};

const std::vector<TestData16Reg1> Reg16TestCases1
{
	{ [](Registers& r) -> auto& { return r.BC; }, 0x01, 0x03, 0x0b, 0x09 },
	{ [](Registers& r) -> auto& { return r.DE; }, 0x11, 0x13, 0x1b, 0x19 },
	{ [](Registers& r) -> auto& { return r.HL; }, 0x21, 0x23, 0x2b, 0x29 },
	{ [](Registers& r) -> auto& { return r.SP; }, 0x31, 0x33, 0x3b, 0x39 }
};

struct TestData16Reg2
{
	Reg16Selector Selector;
	unsigned char StMemAccOpcode;
	unsigned char LdAccMemOpcode;
};

const std::vector<TestData16Reg2> Reg16TestCases2
{
	{ [](Registers& r) -> auto& { return r.BC; }, 0x02, 0x0a },
	{ [](Registers& r) -> auto& { return r.DE; }, 0x12, 0x1a },
	{ [](Registers& r) -> auto& { return r.HL; }, 0x22, 0x2a },
	{ [](Registers& r) -> auto& { return r.HL; }, 0x32, 0x3a }
};

TEST_F(CpuTestFixture, Nop)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0, 0 });

	auto oldReg(Cpu.Registers());

	EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());
	EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

	oldReg.PC += 2;
	EXPECT_EQ(oldReg, Cpu.Registers());
}

TEST_F(CpuTestFixture, Ld16RegImm)
{
	for (auto test : Reg16TestCases1)
	{
		auto& reg = Cpu.Registers();
		reg.PC = 0;
		reg.F = 0;
		test.Selector(reg) = 0;

		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.LdImmOpcode, 0x34, 0x12 });

		auto oldReg{ reg };
		EXPECT_EQ(oldReg, reg);

		EXPECT_EQ(ThreeCycles, Cpu.DoNextInstruction());

		test.Selector(oldReg) = 0x1234;
		oldReg.PC += 3;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, St8MemAcc)
{
	int address = MemoryMap::RamFixed;

	for (unsigned i = 0; i < Reg16TestCases2.size(); i++)
	{
		auto& test = Reg16TestCases2[i];

		auto& reg = Cpu.Registers();
		reg.PC = 0;
		reg.F = 0;
		reg.A = 0x33;
		test.Selector(reg) = address;

		MemoryMap.WriteByte(address, { 0 });
		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.StMemAccOpcode });

		EXPECT_EQ(0, MemoryMap.ReadByte(address));
		auto oldReg{ reg };

		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		oldReg.PC++;

		if (i == 2) oldReg.HL++;
		else if (i == 3) oldReg.HL--;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(0x33, MemoryMap.ReadByte(address));

		address++;
	}
}

TEST_F(CpuTestFixture, Ld8AccMem)
{
	int address = MemoryMap::RamFixed;

	for (unsigned i = 0; i < Reg16TestCases2.size(); i++)
	{
		auto& test = Reg16TestCases2[i];

		auto& reg = Cpu.Registers();
		reg.PC = 0;
		reg.F = 0;
		reg.A = 0;
		test.Selector(reg) = address;

		MemoryMap.WriteByte(address, { 0x88 });
		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.LdAccMemOpcode, test.LdAccMemOpcode });

		EXPECT_EQ(0, reg.A);
		auto oldReg{ reg };

		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		oldReg.PC++;
		oldReg.A = 0x88;

		if (i == 2) oldReg.HL++;
		else if (i == 3) oldReg.HL--;

		EXPECT_EQ(oldReg, reg);

		address++;

		test.Selector(reg) = address;
		MemoryMap.WriteByte(address, { 0x7f });

		oldReg = reg;

		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		oldReg.PC++;
		oldReg.A = 0x7f;

		if (i == 2) oldReg.HL++;
		else if (i == 3) oldReg.HL--;

		EXPECT_EQ(oldReg, reg);

		address++;
	}
}

TEST_F(CpuTestFixture, IncDec16Reg)
{
	for (auto test : Reg16TestCases1)
	{
		auto& reg = Cpu.Registers();
		reg.PC = 0;
		reg.F = 0;
		test.Selector(reg) = 0xffff;

		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.IncOpcode, test.IncOpcode, test.DecOpcode, test.DecOpcode });

		auto oldReg{ reg };
		EXPECT_EQ(oldReg, reg);

		// Inc
		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		test.Selector(oldReg) = 0;
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);

		// Inc
		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		test.Selector(oldReg)++;
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);

		// Dec
		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		test.Selector(oldReg)--;
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);

		// Dec
		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		test.Selector(oldReg) = 0xffff;
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, IncDec8RegOrMem)
{
	for (auto test : Reg8TestCases)
	{
		Cpu.Registers().HL = MemoryMap::RamFixed;

		auto& reg = Cpu.Registers();
		reg.PC = 0;
		test.Selector(reg, MemoryMap) = 0x3F;

		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.IncOpcode, test.DecOpcode });

		auto oldReg{ reg };
		auto oldMem{ MemoryMap };

		// Inc
		EXPECT_EQ(test.IncDecCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem)++;
		oldReg.PC++;
		oldReg.F = HalfCarryFlag;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);
		
		// Dec
		EXPECT_EQ(test.IncDecCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem)--;
		oldReg.PC++;
		oldReg.F = SubFlag | HalfCarryFlag;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);

		// Inc to zero
		reg.PC = 0;
		test.Selector(reg, MemoryMap) = 0xff;

		oldReg = reg;
		oldMem = MemoryMap;

		EXPECT_EQ(test.IncDecCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem) = 0;
		oldReg.PC++;
		oldReg.F = HalfCarryFlag | ZeroFlag;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);

		// Dec to zero
		test.Selector(reg, MemoryMap) = 0x1;
		
		oldReg = reg;
		oldMem = MemoryMap;

		EXPECT_EQ(test.IncDecCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem) = 0;
		oldReg.PC++;
		oldReg.F = ZeroFlag | SubFlag;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);

		// Dec from zero
		test.Selector(reg, MemoryMap) = 0;
		reg.PC = 1;

		oldReg = reg;
		oldMem = MemoryMap;

		EXPECT_EQ(test.IncDecCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem) = 0xff;
		oldReg.PC++;
		oldReg.F = SubFlag | HalfCarryFlag;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);
	}
}

TEST_F(CpuTestFixture, Ld8RegOrMemImm)
{
	for (auto test : Reg8TestCases)
	{
		auto& reg = Cpu.Registers();
	
		// HL points to start of fixed RAM
		reg.HL = MemoryMap::RamFixed;
		reg.PC = 0;
		reg.F = AllFlags;
		test.Selector(reg, MemoryMap) = 0;

		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.LdImmOpcode, 0xaa, test.LdImmOpcode, 0x55 });

		auto oldReg{ reg };
		auto oldMem{ MemoryMap };

		EXPECT_EQ(test.LdCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem) = 0xaa;
		oldReg.PC += 2;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);

		reg.F = 0;
		test.Selector(reg, MemoryMap) = 0;

		EXPECT_EQ(test.LdCycleCount, Cpu.DoNextInstruction());

		test.Selector(oldReg, oldMem) = 0x55;
		oldReg.PC += 2;
		oldReg.F = 0;

		EXPECT_EQ(oldReg, reg);
		EXPECT_EQ(oldMem, MemoryMap);
	}
}

TEST_F(CpuTestFixture, Rlca)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Accum,		Flags,									Result,		Result flags
		{ 0xff,			SubFlag | HalfCarryFlag,				0xff,		CarryFlag },
		{ 0x00,			SubFlag | HalfCarryFlag | CarryFlag,	0x00,		NoFlags },
		{ 0x80,			NoFlags,								0x01,		CarryFlag },
		{ 0x34,			CarryFlag,								0x68,		NoFlags },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x7 });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = test[0];
		reg.F = test[1];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.A = test[2];
		oldReg.F = test[3];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Rla)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Accum,		Flags,									Result,		Result flags
		{ 0xff,			SubFlag | HalfCarryFlag | CarryFlag,	0xff,		CarryFlag },
		{ 0x00,			SubFlag | HalfCarryFlag | CarryFlag,	0x01,		NoFlags },
		{ 0x80,			NoFlags,								0x00,		CarryFlag },
		{ 0x34,			CarryFlag,								0x69,		NoFlags },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x17 });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = test[0];
		reg.F = test[1];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.A = test[2];
		oldReg.F = test[3];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Rrca)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Accum,		Flags,									Result,		Result flags
		{ 0xff,			SubFlag | HalfCarryFlag,				0xff,		CarryFlag },
		{ 0x00,			SubFlag | HalfCarryFlag | CarryFlag,	0x00,		NoFlags },
		{ 0x80,			CarryFlag,								0x40,		NoFlags },
		{ 0x35,			NoFlags,								0x9a,		CarryFlag },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xf });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = test[0];
		reg.F = test[1];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.A = test[2];
		oldReg.F = test[3];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Rra)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Accum,		Flags,									Result,		Result flags
		{ 0xff,			SubFlag | HalfCarryFlag | CarryFlag,	0xff,		CarryFlag },
		{ 0x00,			SubFlag | HalfCarryFlag | CarryFlag,	0x80,		NoFlags },
		{ 0x01,			NoFlags,								0x00,		CarryFlag },
		{ 0x35,			NoFlags,								0x1a,		CarryFlag },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x1f });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = test[0];
		reg.F = test[1];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.A = test[2];
		oldReg.F = test[3];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, St16MemSp)
{
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x8, MemoryMap::RamFixed & 0xff, MemoryMap::RamFixed >> 8,
										0x8, (MemoryMap::RamFixed & 0xff) + 4, MemoryMap::RamFixed >> 8 });

	auto& reg = Cpu.Registers();
	reg.F = 0x0;
	reg.SP = 0x3344;

	auto oldReg{ reg };

	EXPECT_EQ(FiveCycles, Cpu.DoNextInstruction());

	oldReg.PC += 3;

	EXPECT_EQ(oldReg, reg);
	EXPECT_EQ(0x44, MemoryMap.ReadByte(MemoryMap::RamFixed));
	EXPECT_EQ(0x33, MemoryMap.ReadByte(MemoryMap::RamFixed + 1));

	reg.F = AllFlags;
	reg.SP = 0xaabb;

	oldReg = reg;

	EXPECT_EQ(FiveCycles, Cpu.DoNextInstruction());

	oldReg.PC += 3;

	EXPECT_EQ(oldReg, reg);
	EXPECT_EQ(0xbb, MemoryMap.ReadByte(MemoryMap::RamFixed + 4));
	EXPECT_EQ(0xaa, MemoryMap.ReadByte(MemoryMap:: RamFixed + 5));
}

TEST_F(CpuTestFixture, Add16RegHl)
{
	for (auto test : Reg16TestCases1)
	{
		auto& reg = Cpu.Registers();
		reg.PC = 0;
		reg.F = AllFlags;
		reg.HL = 0x8fff;
		test.Selector(reg) = 0x8fff;

		MemoryMap.SetBytes(MemoryMap::RomFixed, { test.AddHlOpcode, test.AddHlOpcode });

		auto oldReg{ reg };
		EXPECT_EQ(oldReg, reg);

		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		oldReg.HL = 0x1ffe;
		oldReg.PC++;
		oldReg.F = ZeroFlag | HalfCarryFlag | CarryFlag;

		EXPECT_EQ(oldReg, reg);

		reg.HL = 0x1234;
		test.Selector(reg) = 0x1234;

		oldReg = reg;

		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		oldReg.HL = 0x2468;
		oldReg.F = ZeroFlag;
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Daa)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Accum,		Flags,									Result,		Result flags
		{ 0x49 + 0x19,	HalfCarryFlag,							0x68,		NoFlags },
		{ 0x99 + 0x01,	NoFlags,								0x00,		ZeroFlag | CarryFlag },
		{ 0xff,			SubFlag | HalfCarryFlag | CarryFlag,	0x99,		SubFlag | CarryFlag	},
		{ 0x66,			SubFlag | HalfCarryFlag | CarryFlag,	0x00,		SubFlag | CarryFlag | ZeroFlag },
		{ 0x63 - 0x11,	SubFlag,								0x52,		SubFlag	},
		{ 0x61 - 0x39,	SubFlag | HalfCarryFlag,				0x22,		SubFlag	},
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x27 });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = test[0];
		reg.F = test[1];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.A = test[2];
		oldReg.F = test[3];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Cpl)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Accum,	Flags,						Result
		{ 0x37,		ZeroFlag | CarryFlag,		0xc8 },
		{ 0x37,		NoFlags,					0xc8 },
		{ 0xaa,		ZeroFlag | CarryFlag,		0x55 },
		{ 0xaa,		SubFlag | HalfCarryFlag,	0x55 },
		{ 0x00,		ZeroFlag | CarryFlag,		0xff },
		{ 0x00,		NoFlags,					0xff },
		{ 0xff,		ZeroFlag | CarryFlag,		0x00 },
		{ 0xff,		NoFlags,					0x00 },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x2f });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = test[0];
		reg.F = test[1];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.A = test[2];
		oldReg.F = test[1] | SubFlag | HalfCarryFlag;
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Scf)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Flags,					Result Flags
		{ ZeroFlag | CarryFlag,		ZeroFlag | CarryFlag },
		{ NoFlags,					CarryFlag },
		{ AllFlags,					ZeroFlag | CarryFlag },
		{ SubFlag | HalfCarryFlag,	CarryFlag },
		{ CarryFlag,				CarryFlag },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x37 });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = 0xaa;
		reg.F = test[0];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.F = test[1];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Ccf)
{
	const std::vector<std::vector<unsigned char>> tests =
	{
		// Flags,					Result Flags
		{ ZeroFlag | CarryFlag,		ZeroFlag },
		{ NoFlags,					CarryFlag },
		{ AllFlags,					ZeroFlag },
		{ SubFlag | HalfCarryFlag,	CarryFlag },
		{ CarryFlag,				NoFlags },
	};

	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0x3f });
	auto& reg = Cpu.Registers();

	for (auto& test : tests)
	{
		reg.PC = 0;
		reg.A = 0xaa;
		reg.F = test[0];

		auto oldReg{ reg };

		EXPECT_EQ(OneCycle, Cpu.DoNextInstruction());

		oldReg.F = test[1];
		oldReg.PC++;

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Ld8RegOrMemRegOrMem)
{
	const std::vector<std::tuple<unsigned char, int, std::function<void(Registers&, unsigned char)>, std::function<unsigned char(Registers&)>>> tests =
	{
		// Opcode	Cycle count		Set source fn																Get Dest fn
		{ 0x50,		OneCycle,		[](Registers& r, unsigned char val) { r.B = val; },							 [](Registers& r) { return r.D; } },
		{ 0x41,		OneCycle,		[](Registers& r, unsigned char val) { r.C = val; },							 [](Registers& r) { return r.B; } },
		{ 0x62,		OneCycle,		[](Registers& r, unsigned char val) { r.D = val; },							 [](Registers& r) { return r.H; } },
		{ 0x73,		TwoCycles,		[](Registers& r, unsigned char val) { r.E = val; },							 [&](Registers& r) { return MemoryMap.ReadByte(r.HL); } },
		{ 0x44,		OneCycle,		[](Registers& r, unsigned char val) { r.H = val; },							 [](Registers& r) { return r.B; } },
		{ 0x55,		OneCycle,		[](Registers& r, unsigned char val) { r.L = val; },							 [](Registers& r) { return r.D; } },
		{ 0x66,		TwoCycles,		[&](Registers& r, unsigned char val) { this->MemoryMap.WriteByte(r.HL, val); }, [](Registers& r) { return r.H; } },
		{ 0x77,		TwoCycles,		[](Registers& r, unsigned char val) { r.A = val; },							 [&](Registers& r) { return MemoryMap.ReadByte(r.HL); } },

		{ 0x48,		OneCycle,		[](Registers& r, unsigned char val) { r.B = val; },							 [](Registers& r) { return r.C; } },
		{ 0x59,		OneCycle,		[](Registers& r, unsigned char val) { r.C = val; },							 [](Registers& r) { return r.E; } },
		{ 0x6a,		OneCycle,		[](Registers& r, unsigned char val) { r.D = val; },							 [](Registers& r) { return r.L; } },
		{ 0x7b,		OneCycle,		[](Registers& r, unsigned char val) { r.E = val; },							 [](Registers& r) { return r.A; } },
		{ 0x4c,		OneCycle,		[](Registers& r, unsigned char val) { r.H = val; },							 [](Registers& r) { return r.C; } },
		{ 0x5d,		OneCycle,		[](Registers& r, unsigned char val) { r.L = val; },							 [](Registers& r) { return r.E; } },
		{ 0x7e,		TwoCycles,		[&](Registers& r, unsigned char val) { this->MemoryMap.WriteByte(r.HL, val); }, [](Registers& r) { return r.A; } },
		{ 0x6f,		OneCycle,		[](Registers& r, unsigned char val) { r.A = val; },							 [](Registers& r) { return r.L; } },
	};

	for (auto& test : tests)
	{
		MemoryMap.SetBytes(MemoryMap::RomFixed, { std::get<0>(test) });
		
		for (unsigned char testVal : { 0xaa, 0x33 })
		{
			auto& reg = Cpu.Registers();

			reg.PC = 0;
			reg.HL = MemoryMap::RamFixed;
			std::get<2>(test)(reg, testVal);
			reg.F = 0;

			EXPECT_EQ(std::get<1>(test), Cpu.DoNextInstruction());

			EXPECT_EQ(testVal, std::get<3>(test)(reg));
			EXPECT_EQ(0, reg.F);
			EXPECT_EQ(1, reg.PC);
		}
	}
}

TEST_F(CpuTestFixture, AluOp8AccRegOrMem)
{
	const std::vector<std::tuple<int, std::function<void(Registers&, unsigned char)>>> sources =
	{
		// Cycle count	Set source fn
		{ OneCycle,		[](Registers& r, unsigned char val) { r.B = val; } },
		{ OneCycle,		[](Registers& r, unsigned char val) { r.C = val; } },
		{ OneCycle,		[](Registers& r, unsigned char val) { r.D = val; } },
		{ OneCycle,		[](Registers& r, unsigned char val) { r.E = val; } },
		{ OneCycle,		[](Registers& r, unsigned char val) { r.H = val; } },
		{ OneCycle,		[](Registers& r, unsigned char val) { r.L = val; } },
		{ TwoCycles,	[&](Registers& r, unsigned char val) { this->MemoryMap.WriteByte(r.HL, val); } },
		{ OneCycle,		[](Registers& r, unsigned char val) { r.A = val; } }
	};

	enum Operation : unsigned char
	{
		Add = 0x80, Adc = 0x88,
		Sub = 0x90,	Sbc = 0x98,
		And = 0xa0,	Xor = 0xa8,
		Or  = 0xb0,	Cp  = 0xb8
	};

	const std::vector<std::vector<unsigned char>> tests =
	{
		// Opcode	Operand1	Operand2	Flags in	Result	Flags out
		{ Add,		0x0,		0x0,		CarryFlag,	0x0,	ZeroFlag },
		{ Add,		0x99,		0x3a,		CarryFlag,	0xd3,	HalfCarryFlag },
		{ Add,		0x80,		0x80,		NoFlags,	0x0,	ZeroFlag | CarryFlag },
		{ Add,		0xff,		0x2,		AllFlags,	0x1,	CarryFlag | HalfCarryFlag },
		{ Add,		0xff,		0x1,		AllFlags,	0x0,	ZeroFlag | CarryFlag | HalfCarryFlag },

		{ Adc,		0x0,		0x0,		NoFlags,	0x0,	ZeroFlag },
		{ Adc,		0x0,		0x0,		CarryFlag,	0x1,	NoFlags },
		{ Adc,		0x99,		0x3a,		NoFlags,	0xd3,	HalfCarryFlag },
		{ Adc,		0x99,		0x3a,		CarryFlag,	0xd4,	HalfCarryFlag },
		{ Adc,		0x80,		0x80,		NoFlags,	0x0,	ZeroFlag | CarryFlag },
		{ Adc,		0x80,		0x80,		CarryFlag,	0x1,	CarryFlag },
		{ Adc,		0xff,		0x2,		AllFlags,	0x2,	CarryFlag | HalfCarryFlag },
		{ Adc,		0xff,		0x0,		CarryFlag,	0x0,	ZeroFlag | CarryFlag | HalfCarryFlag },

		{ Sub,		0x0,		0x0,		CarryFlag,	0x0,	SubFlag | ZeroFlag },
		{ Sub,		0x0,		0x1,		NoFlags,	0xff,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sub,		0x99,		0x3a,		CarryFlag,	0x5f,	SubFlag | HalfCarryFlag },
		{ Sub,		0x80,		0x80,		NoFlags,	0x0,	SubFlag | ZeroFlag },
		{ Sub,		0x1,		0x1,		AllFlags,	0x0,	SubFlag | ZeroFlag },

		{ Sbc,		0x0,		0x0,		NoFlags,	0x0,	SubFlag | ZeroFlag },
		{ Sbc,		0x0,		0x0,		CarryFlag,	0x0ff,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sbc,		0x0,		0x1,		NoFlags,	0xff,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sbc,		0x0,		0x1,		CarryFlag,	0xfe,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sbc,		0x99,		0x3a,		NoFlags,	0x5f,	SubFlag | HalfCarryFlag },
		{ Sbc,		0x99,		0x3a,		CarryFlag,	0x5e,	SubFlag | HalfCarryFlag },
		{ Sbc,		0x80,		0x80,		NoFlags,	0x0,	SubFlag | ZeroFlag },
		{ Sbc,		0x1,		0x1,		AllFlags,	0xff,	SubFlag | CarryFlag | HalfCarryFlag },

		{ And,		0x0,		0x0ff,		CarryFlag,	0x0,	HalfCarryFlag | ZeroFlag },
		{ And,		0x0ff,		0x00,		CarryFlag,	0x0,	HalfCarryFlag | ZeroFlag },
		{ And,		0xa5,		0xa5,		CarryFlag,	0xa5,	HalfCarryFlag },
		{ And,		0x5a,		0xa5,		CarryFlag,	0x0,	HalfCarryFlag | ZeroFlag },
		{ And,		0x69,		0xff,		NoFlags,	0x69,	HalfCarryFlag },
		{ And,		0xff,		0x69,		AllFlags,	0x69,	HalfCarryFlag },
		{ And,		0x6a,		0xbc,		NoFlags,	0x28,	HalfCarryFlag },
		{ And,		0xff,		0xff,		NoFlags,	0xff,	HalfCarryFlag },

		{ Xor,		0x0,		0x0ff,		CarryFlag,	0x0ff,	NoFlags },
		{ Xor,		0x0ff,		0x00,		CarryFlag,	0x0ff,	NoFlags },
		{ Xor,		0xa5,		0xa5,		CarryFlag,	0x0,	ZeroFlag },
		{ Xor,		0x5a,		0xa5,		CarryFlag,	0x0ff,	NoFlags },
		{ Xor,		0x69,		0xff,		NoFlags,	0x96,	NoFlags },
		{ Xor,		0xff,		0x69,		AllFlags,	0x96,	NoFlags },
		{ Xor,		0x6a,		0xbc,		NoFlags,	0xd6,	NoFlags },
		{ Xor,		0xff,		0xff,		NoFlags,	0x0,	ZeroFlag },

		{ Or,		0x0,		0x0ff,		CarryFlag,	0x0ff,	NoFlags },
		{ Or,		0x0ff,		0x00,		CarryFlag,	0x0ff,	NoFlags },
		{ Or,		0xa5,		0xa5,		CarryFlag,	0xa5,	NoFlags },
		{ Or,		0x5a,		0xa5,		CarryFlag,	0xff,	NoFlags },
		{ Or,		0x69,		0xff,		NoFlags,	0xff,	NoFlags },
		{ Or,		0x30,		0x05,		AllFlags,	0x35,	NoFlags },
		{ Or,		0x6a,		0xbc,		NoFlags,	0xfe,	NoFlags },
		{ Or,		0xff,		0xff,		NoFlags,	0xff,	NoFlags },

		{ Cp,		0x0,		0x0,		CarryFlag,	0x0,	SubFlag | ZeroFlag },
		{ Cp,		0x0,		0x1,		NoFlags,	0x0,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Cp,		0x99,		0x3a,		CarryFlag,	0x99,	SubFlag | HalfCarryFlag },
		{ Cp,		0x80,		0x80,		NoFlags,	0x80,	SubFlag | ZeroFlag },
		{ Cp,		0x1,		0x1,		AllFlags,	0x1,	SubFlag | ZeroFlag },
	};

	for (unsigned i=0; i<sources.size(); i++)
	{
		for (auto& test : tests)
		{
			// Only use A as source if both operands are identical
			if (i != 7 || test[1] == test[2])
			{
				MemoryMap.SetBytes(MemoryMap::RomFixed, { static_cast<unsigned char>(test[0] | i) });

				auto& reg = Cpu.Registers();

				reg.PC = 0;
				reg.HL = MemoryMap::RamFixed;
				reg.A = test[1];
				reg.F = test[3];
				std::get<1>(sources[i])(reg, test[2]);

				EXPECT_EQ(std::get<0>(sources[i]), Cpu.DoNextInstruction());

				EXPECT_EQ(test[4], reg.A);
				EXPECT_EQ(test[5], reg.F);
				EXPECT_EQ(1, reg.PC);
			}
		}
	}
}

TEST_F(CpuTestFixture, AluOp8AccImm)
{
	enum Operation : unsigned char
	{
		Add = 0xc6, Adc = 0xce,
		Sub = 0xd6, Sbc = 0xde,
		And = 0xe6, Xor = 0xee,
		Or  = 0xf6, Cp  = 0xfe
	};

	const std::vector<std::vector<unsigned char>> tests =
	{
		// Opcode	Operand1	Operand2	Flags in	Result	Flags out
		{ Add,		0x0,		0x0,		CarryFlag,	0x0,	ZeroFlag },
		{ Add,		0x99,		0x3a,		CarryFlag,	0xd3,	HalfCarryFlag },
		{ Add,		0x80,		0x80,		NoFlags,	0x0,	ZeroFlag | CarryFlag },
		{ Add,		0xff,		0x2,		AllFlags,	0x1,	CarryFlag | HalfCarryFlag },
		{ Add,		0xff,		0x1,		AllFlags,	0x0,	ZeroFlag | CarryFlag | HalfCarryFlag },

		{ Adc,		0x0,		0x0,		NoFlags,	0x0,	ZeroFlag },
		{ Adc,		0x0,		0x0,		CarryFlag,	0x1,	NoFlags },
		{ Adc,		0x99,		0x3a,		NoFlags,	0xd3,	HalfCarryFlag },
		{ Adc,		0x99,		0x3a,		CarryFlag,	0xd4,	HalfCarryFlag },
		{ Adc,		0x80,		0x80,		NoFlags,	0x0,	ZeroFlag | CarryFlag },
		{ Adc,		0x80,		0x80,		CarryFlag,	0x1,	CarryFlag },
		{ Adc,		0xff,		0x2,		AllFlags,	0x2,	CarryFlag | HalfCarryFlag },
		{ Adc,		0xff,		0x0,		CarryFlag,	0x0,	ZeroFlag | CarryFlag | HalfCarryFlag },

		{ Sub,		0x0,		0x0,		CarryFlag,	0x0,	SubFlag | ZeroFlag },
		{ Sub,		0x0,		0x1,		NoFlags,	0xff,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sub,		0x99,		0x3a,		CarryFlag,	0x5f,	SubFlag | HalfCarryFlag },
		{ Sub,		0x80,		0x80,		NoFlags,	0x0,	SubFlag | ZeroFlag },
		{ Sub,		0x1,		0x1,		AllFlags,	0x0,	SubFlag | ZeroFlag },

		{ Sbc,		0x0,		0x0,		NoFlags,	0x0,	SubFlag | ZeroFlag },
		{ Sbc,		0x0,		0x0,		CarryFlag,	0x0ff,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sbc,		0x0,		0x1,		NoFlags,	0xff,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sbc,		0x0,		0x1,		CarryFlag,	0xfe,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Sbc,		0x99,		0x3a,		NoFlags,	0x5f,	SubFlag | HalfCarryFlag },
		{ Sbc,		0x99,		0x3a,		CarryFlag,	0x5e,	SubFlag | HalfCarryFlag },
		{ Sbc,		0x80,		0x80,		NoFlags,	0x0,	SubFlag | ZeroFlag },
		{ Sbc,		0x1,		0x1,		AllFlags,	0xff,	SubFlag | CarryFlag | HalfCarryFlag },

		{ And,		0x0,		0x0ff,		CarryFlag,	0x0,	HalfCarryFlag | ZeroFlag },
		{ And,		0x0ff,		0x00,		CarryFlag,	0x0,	HalfCarryFlag | ZeroFlag },
		{ And,		0xa5,		0xa5,		CarryFlag,	0xa5,	HalfCarryFlag },
		{ And,		0x5a,		0xa5,		CarryFlag,	0x0,	HalfCarryFlag | ZeroFlag },
		{ And,		0x69,		0xff,		NoFlags,	0x69,	HalfCarryFlag },
		{ And,		0xff,		0x69,		AllFlags,	0x69,	HalfCarryFlag },
		{ And,		0x6a,		0xbc,		NoFlags,	0x28,	HalfCarryFlag },
		{ And,		0xff,		0xff,		NoFlags,	0xff,	HalfCarryFlag },

		{ Xor,		0x0,		0x0ff,		CarryFlag,	0x0ff,	NoFlags },
		{ Xor,		0x0ff,		0x00,		CarryFlag,	0x0ff,	NoFlags },
		{ Xor,		0xa5,		0xa5,		CarryFlag,	0x0,	ZeroFlag },
		{ Xor,		0x5a,		0xa5,		CarryFlag,	0x0ff,	NoFlags },
		{ Xor,		0x69,		0xff,		NoFlags,	0x96,	NoFlags },
		{ Xor,		0xff,		0x69,		AllFlags,	0x96,	NoFlags },
		{ Xor,		0x6a,		0xbc,		NoFlags,	0xd6,	NoFlags },
		{ Xor,		0xff,		0xff,		NoFlags,	0x0,	ZeroFlag },

		{ Or,		0x0,		0x0ff,		CarryFlag,	0x0ff,	NoFlags },
		{ Or,		0x0ff,		0x00,		CarryFlag,	0x0ff,	NoFlags },
		{ Or,		0xa5,		0xa5,		CarryFlag,	0xa5,	NoFlags },
		{ Or,		0x5a,		0xa5,		CarryFlag,	0xff,	NoFlags },
		{ Or,		0x69,		0xff,		NoFlags,	0xff,	NoFlags },
		{ Or,		0x30,		0x05,		AllFlags,	0x35,	NoFlags },
		{ Or,		0x6a,		0xbc,		NoFlags,	0xfe,	NoFlags },
		{ Or,		0xff,		0xff,		NoFlags,	0xff,	NoFlags },

		{ Cp,		0x0,		0x0,		CarryFlag,	0x0,	SubFlag | ZeroFlag },
		{ Cp,		0x0,		0x1,		NoFlags,	0x0,	SubFlag | CarryFlag | HalfCarryFlag },
		{ Cp,		0x99,		0x3a,		CarryFlag,	0x99,	SubFlag | HalfCarryFlag },
		{ Cp,		0x80,		0x80,		NoFlags,	0x80,	SubFlag | ZeroFlag },
		{ Cp,		0x1,		0x1,		AllFlags,	0x1,	SubFlag | ZeroFlag },
	};

	for (auto& test : tests)
	{
		MemoryMap.SetBytes(MemoryMap::RomFixed, { test[0], test[2] });

		auto& reg = Cpu.Registers();

		reg.PC = 0;
		reg.A = test[1];
		reg.F = test[3];

		EXPECT_EQ(TwoCycles, Cpu.DoNextInstruction());

		EXPECT_EQ(test[4], reg.A);
		EXPECT_EQ(test[5], reg.F);
		EXPECT_EQ(2, reg.PC);
	}
}

TEST_F(CpuTestFixture, Ret)
{
	const std::vector<std::tuple<unsigned char, unsigned char, bool, int>> tests =
	{
		// Opcode	Flags		Taken	Cycle count
		// RET NZ
		{ 0xc0,		NoFlags,	true,	20 },
		{ 0xc0,		~ZeroFlag,	true,	20 },
		{ 0xc0,		ZeroFlag,	false,	8 },
		{ 0xc0,		AllFlags,	false,	8 },

		// RET Z
		{ 0xc8,		NoFlags,	false,	8 },
		{ 0xc8,		~ZeroFlag,	false,	8 },
		{ 0xc8,		ZeroFlag,	true,	20 },
		{ 0xc8,		AllFlags,	true,	20 },

		// RET NC
		{ 0xd0,		NoFlags,	true,	20 },
		{ 0xd0,		~CarryFlag,	true,	20 },
		{ 0xd0,		CarryFlag,	false,	8 },
		{ 0xd0,		AllFlags,	false,	8 },

		// RET C
		{ 0xd8,		NoFlags,	false,	8 },
		{ 0xd8,		~CarryFlag,	false,	8 },
		{ 0xd8,		CarryFlag,	true,	20 },
		{ 0xd8,		AllFlags,	true,	20 },

		// RET
		{ 0xc9,		NoFlags,	true,	16 },
		{ 0xc9,		~CarryFlag,	true,	16 },
		{ 0xc9,		CarryFlag,	true,	16 },
		{ 0xc9,		ZeroFlag,	true,	16 },
		{ 0xc9,		AllFlags,	true,	16 },

		// RETI
		{ 0xd9,		NoFlags,	true,	16 },
		{ 0xd9,		~CarryFlag,	true,	16 },
		{ 0xd9,		CarryFlag,	true,	16 },
		{ 0xd9,		ZeroFlag,	true,	16 },
		{ 0xd9,		AllFlags,	true,	16 },
	};

	for (auto& test : tests)
	{
		auto& interruptsEnabled = Cpu.InterruptsEnabled();
		interruptsEnabled = false;

		MemoryMap.SetBytes(MemoryMap::RomFixed, { std::get<0>(test) });

		// Return address stored on stack
		MemoryMap.SetBytes(MemoryMap::RamFixed, { 0x34, 0x12 });

		auto& reg = Cpu.Registers();

		reg.PC = 0;
		reg.SP = MemoryMap::RamFixed;
		reg.F = std::get<1>(test);

		auto oldReg{ reg };

		EXPECT_FALSE(interruptsEnabled);
		EXPECT_EQ(std::get<3>(test), Cpu.DoNextInstruction());

		if (std::get<2>(test))
		{
			oldReg.PC = 0x1234;
			oldReg.SP += 2;
		}
		else
		{
			oldReg.PC = 1;
		}

		EXPECT_EQ(oldReg, reg);

		// Ensure RETI has re-enabled interrupts
		EXPECT_TRUE(std::get<0>(test) != 0xd9 || interruptsEnabled);
	}
}

TEST_F(CpuTestFixture, Pop)
{
	const std::vector<std::tuple<unsigned char, unsigned short&(*)(Registers&), unsigned short>> scenarios
	{
		{ 0xc1, [](Registers& r) -> auto& { return r.BC; }, 0xffff },
		{ 0xd1, [](Registers& r) -> auto& { return r.DE; }, 0xffff },
		{ 0xe1, [](Registers& r) -> auto& { return r.HL; }, 0xffff },
		{ 0xf1, [](Registers& r) -> auto& { return r.AF; }, 0xfff0 }
	};

	for (auto& scenario : scenarios)
	{
		MemoryMap.SetBytes(MemoryMap::RomFixed, { std::get<0>(scenario) });

		for (unsigned short testVal : { 0x1234, 0xa9f3, 0xfedc })
		{
			// Data to be popped stored on stack
			MemoryMap.SetBytes(MemoryMap::RamFixed, { static_cast<unsigned char>(testVal & 0xff),
												static_cast<unsigned char>(testVal >> 8) });

			auto& registers = Cpu.Registers();
			auto& destRegFn = std::get<1>(scenario);

			registers.PC = 0;
			registers.SP = MemoryMap::RamFixed;
			destRegFn(registers) = 0;

			auto oldRegisters{ registers };

			EXPECT_EQ(ThreeCycles, Cpu.DoNextInstruction());

			oldRegisters.PC = 1;
			oldRegisters.SP += 2;
			destRegFn(oldRegisters) = testVal & std::get<2>(scenario);

			EXPECT_EQ(oldRegisters, registers);
		}
	}
}

TEST_F(CpuTestFixture, Push)
{
	const std::vector<std::tuple<unsigned char, unsigned short&(*)(Registers&)>> scenarios
	{
		{ 0xc5, [](Registers& r) -> auto& { return r.BC; } },
		{ 0xd5, [](Registers& r) -> auto& { return r.DE; } },
		{ 0xe5, [](Registers& r) -> auto& { return r.HL; } },
		{ 0xf5, [](Registers& r) -> auto& { return r.AF; } }
	};

	for (auto& scenario : scenarios)
	{
		MemoryMap.SetBytes(MemoryMap::RomFixed, { std::get<0>(scenario) });

		for (unsigned short testVal : { 0x1234, 0xa9f3, 0xfedc })
		{
			// Clear stack
			MemoryMap.SetBytes(MemoryMap::RamFixed, { 0, 0 });

			EXPECT_EQ(0, MemoryMap.ReadByte(MemoryMap::RamFixed));
			EXPECT_EQ(0, MemoryMap.ReadByte(MemoryMap::RamFixed + 1));

			auto& registers = Cpu.Registers();
			auto& destRegFn = std::get<1>(scenario);

			registers.PC = 0;
			registers.SP = MemoryMap::RamFixed + 2;
			destRegFn(registers) = testVal;

			auto oldRegisters{ registers };

			EXPECT_EQ(FourCycles, Cpu.DoNextInstruction());

			oldRegisters.PC = 1;
			oldRegisters.SP -= 2;

			EXPECT_EQ(oldRegisters, registers);

			EXPECT_EQ(testVal & 0xff, MemoryMap.ReadByte(MemoryMap::RamFixed));
			EXPECT_EQ(testVal >> 8, MemoryMap.ReadByte(MemoryMap::RamFixed + 1));
		}
	}
}

TEST_F(CpuTestFixture, JpCall)
{
	const std::vector<std::tuple<unsigned char, unsigned char, bool, bool>> tests =
	{
		// Opcode	Flags		Is call		Taken
		// JP NZ
		{ 0xc2,		NoFlags,	false,		true },
		{ 0xc2,		~ZeroFlag,	false,		true },
		{ 0xc2,		ZeroFlag,	false,		false },
		{ 0xc2,		AllFlags,	false,		false },

		// JP Z
		{ 0xca,		NoFlags,	false,		false },
		{ 0xca,		~ZeroFlag,	false,		false },
		{ 0xca,		ZeroFlag,	false,		true },
		{ 0xca,		AllFlags,	false,		true },

		// JP NC
		{ 0xd2,		NoFlags,	false,		true },
		{ 0xd2,		~CarryFlag,	false,		true },
		{ 0xd2,		CarryFlag,	false,		false },
		{ 0xd2,		AllFlags,	false,		false },

		// JP C
		{ 0xda,		NoFlags,	false,		false },
		{ 0xda,		~CarryFlag,	false,		false },
		{ 0xda,		CarryFlag,	false,		true },
		{ 0xda,		AllFlags,	false,		true },

		// JP
		{ 0xc3,		NoFlags,	false,		true },
		{ 0xc3,		~CarryFlag,	false,		true },
		{ 0xc3,		CarryFlag,	false,		true },
		{ 0xc3,		ZeroFlag,	false,		true },
		{ 0xc3,		AllFlags,	false,		true },

		// CALL NZ
		{ 0xc4,		NoFlags,	true,		true },
		{ 0xc4,		~ZeroFlag,	true,		true },
		{ 0xc4,		ZeroFlag,	true,		false },
		{ 0xc4,		AllFlags,	true,		false },

		// CALL Z
		{ 0xcc,		NoFlags,	true,		false },
		{ 0xcc,		~ZeroFlag,	true,		false },
		{ 0xcc,		ZeroFlag,	true,		true },
		{ 0xcc,		AllFlags,	true,		true },

		// CALL NC
		{ 0xd4,		NoFlags,	true,		true },
		{ 0xd4,		~CarryFlag,	true,		true },
		{ 0xd4,		CarryFlag,	true,		false },
		{ 0xd4,		AllFlags,	true,		false },

		// CALL C
		{ 0xdc,		NoFlags,	true,		false },
		{ 0xdc,		~CarryFlag,	true,		false },
		{ 0xdc,		CarryFlag,	true,		true },
		{ 0xdc,		AllFlags,	true,		true },

		// CALL
		{ 0xcd,		NoFlags,	true,		true },
		{ 0xcd,		~CarryFlag,	true,		true },
		{ 0xcd,		CarryFlag,	true,		true },
		{ 0xcd,		ZeroFlag,	true,		true },
		{ 0xcd,		AllFlags,	true,		true },
	};

	for (auto& test : tests)
	{
		MemoryMap.SetBytes(MemoryMap::RamFixed, { std::get<0>(test), 0x34, 0x12 });

		const unsigned short stackStart = MemoryMap::RamFixed + 3;

		// Clear stack
		MemoryMap.SetBytes(stackStart, { 0, 0 });

		auto& reg = Cpu.Registers();

		reg.PC = MemoryMap::RamFixed;
		reg.SP = stackStart + 2;
		reg.F = std::get<1>(test);

		auto oldReg{ reg };

		auto cycleCount = std::get<3>(test)
							? std::get<2>(test)	? SixCycles	: FourCycles
							: ThreeCycles;

		EXPECT_EQ(cycleCount, Cpu.DoNextInstruction());

		if (std::get<3>(test))
		{
			oldReg.PC = 0x1234;

			if (std::get<2>(test))
			{
				oldReg.SP = stackStart;

				// Check address of next byte after call instruction (which is also stack start) is on stack
				EXPECT_EQ(stackStart & 0xff, MemoryMap.ReadByte(stackStart));
				EXPECT_EQ(stackStart >> 8, MemoryMap.ReadByte(stackStart + 1));
			}
		}
		else
		{
			oldReg.PC = stackStart;
		}

		EXPECT_EQ(oldReg, reg);
	}
}

TEST_F(CpuTestFixture, Rst)
{
	std::vector<std::vector<unsigned char>> tests =
	{
		// Opcode	Dest. address
		{ 0xc7,		0x0  },
		{ 0xd7,		0x10 },
		{ 0xe7,		0x20 },
		{ 0xf7,		0x30 },

		{ 0xcf,		0x08 },
		{ 0xdf,		0x18 },
		{ 0xef,		0x28 },
		{ 0xff,		0x38 },
	};

	for (auto& test : tests)
	{
		MemoryMap.SetBytes(MemoryMap::RamFixed, { test[0] });

		const unsigned short stackStart = MemoryMap::RamFixed + 1;

		// Clear stack
		MemoryMap.SetBytes(stackStart, { 0, 0 });

		auto& reg = Cpu.Registers();

		reg.PC = MemoryMap::RamFixed;
		reg.SP = stackStart + 2;

		auto oldReg{ reg };

		EXPECT_EQ(FourCycles, Cpu.DoNextInstruction());

		oldReg.PC = test[1];
		oldReg.SP = stackStart;

		// Check address of next byte after RST instruction (which is also stack start) is on stack
		EXPECT_EQ(stackStart & 0xff, MemoryMap.ReadByte(stackStart));
		EXPECT_EQ(stackStart >> 8, MemoryMap.ReadByte(stackStart + 1));

		EXPECT_EQ(oldReg, reg);
	}
}


TEST_F(CpuTestFixture, CbPrefixOpcodes)
{
	const std::vector<std::tuple<int, std::function<unsigned char(Registers&)>, std::function<void(Registers&, unsigned char)>>> sources =
	{
		// Cycle count	Get function											Set function
		{ TwoCycles,	[](Registers& r) { return r.B; },						[](Registers& r, unsigned char val) { r.B = val; } },
		{ TwoCycles,	[](Registers& r) { return r.C; },						[](Registers& r, unsigned char val) { r.C = val; } },
		{ TwoCycles,	[](Registers& r) { return r.D; },						[](Registers& r, unsigned char val) { r.D = val; } },
		{ TwoCycles,	[](Registers& r) { return r.E; },						[](Registers& r, unsigned char val) { r.E = val; } },
		{ TwoCycles,	[](Registers& r) { return r.H; },						[](Registers& r, unsigned char val) { r.H = val; } },
		{ TwoCycles,	[](Registers& r) { return r.L; },						[](Registers& r, unsigned char val) { r.L = val; } },
		{ FourCycles,	[&](Registers& r) { return MemoryMap.ReadByte(r.HL); },	[&](Registers& r, unsigned char val) { this->MemoryMap.WriteByte(r.HL, val); } },
		{ TwoCycles,	[](Registers& r) { return r.A; },						[](Registers& r, unsigned char val) { r.A = val; } }
	};

	enum Operation : unsigned char
	{
		Rlc  = 0x00, Rrc = 0x08,
		Rl   = 0x10, Rr  = 0x18,
		Sla  = 0x20, Sra = 0x28,
		Swap = 0x30, Srl = 0x38,
		Bit  = 0x40,
		Res  = 0x80,
		Set  = 0xc0
	};

	const std::vector<std::vector<unsigned char>> tests =
	{
		// Input	Flags in	Operation	[Bit no.]	Result	Flags out
		{ 0x0,		CarryFlag,	Rlc,		0,			0x0,	ZeroFlag },
		{ 0xff,		NoFlags,	Rlc,		0,			0xff,	CarryFlag },
		{ 0x7f,		NoFlags,	Rlc,		0,			0xfe,	NoFlags },
		{ 0x80,		CarryFlag,	Rlc,		0,			0x1,	CarryFlag },
		{ 0x59,		CarryFlag,	Rlc,		0,			0xb2,	NoFlags },

		{ 0x0,		CarryFlag,	Rrc,		0,			0x0,	ZeroFlag },
		{ 0xff,		NoFlags,	Rrc,		0,			0xff,	CarryFlag },
		{ 0xfe,		NoFlags,	Rrc,		0,			0x7f,	NoFlags },
		{ 0x01,		CarryFlag,	Rrc,		0,			0x80,	CarryFlag },
		{ 0xb2,		CarryFlag,	Rrc,		0,			0x59,	NoFlags },

		{ 0x0,		NoFlags,	Rl,			0,			0x0,	ZeroFlag },
		{ 0x0,		CarryFlag,	Rl,			0,			0x01,	NoFlags },
		{ 0xff,		NoFlags,	Rl,			0,			0xfe,	CarryFlag },
		{ 0xff,		CarryFlag,	Rl,			0,			0xff,	CarryFlag },
		{ 0x7f,		NoFlags,	Rl,			0,			0xfe,	NoFlags },
		{ 0x80,		CarryFlag,	Rl,			0,			0x1,	CarryFlag },
		{ 0x80,		NoFlags,	Rl,			0,			0x0,	ZeroFlag | CarryFlag },
		{ 0x59,		CarryFlag,	Rl,			0,			0xb3,	NoFlags },

		{ 0x0,		NoFlags,	Rr,			0,			0x0,	ZeroFlag },
		{ 0x0,		CarryFlag,	Rr,			0,			0x80,	NoFlags },
		{ 0xff,		NoFlags,	Rr,			0,			0x7f,	CarryFlag },
		{ 0xff,		CarryFlag,	Rr,			0,			0xff,	CarryFlag },
		{ 0xfe,		NoFlags,	Rr,			0,			0x7f,	NoFlags },
		{ 0x01,		CarryFlag,	Rr,			0,			0x80,	CarryFlag },
		{ 0x01,		NoFlags,	Rr,			0,			0x0,	ZeroFlag | CarryFlag },
		{ 0xb2,		CarryFlag,	Rr,			0,			0xd9,	NoFlags },

		{ 0x0,		CarryFlag,	Sla,		0,			0x0,	ZeroFlag },
		{ 0xff,		NoFlags,	Sla,		0,			0xfe,	CarryFlag },
		{ 0x7f,		NoFlags,	Sla,		0,			0xfe,	NoFlags },
		{ 0x80,		CarryFlag,	Sla,		0,			0x0,	ZeroFlag | CarryFlag },
		{ 0x59,		CarryFlag,	Sla,		0,			0xb2,	NoFlags },

		{ 0x0,		CarryFlag,	Sra,		0,			0x0,	ZeroFlag },
		{ 0xff,		NoFlags,	Sra,		0,			0xff,	CarryFlag },
		{ 0xfe,		NoFlags,	Sra,		0,			0xff,	NoFlags },
		{ 0x01,		CarryFlag,	Sra,		0,			0x0,	ZeroFlag | CarryFlag },
		{ 0xb2,		CarryFlag,	Sra,		0,			0xd9,	NoFlags },

		{ 0x0,		CarryFlag,	Swap,		0,			0x0,	ZeroFlag },
		{ 0xff,		NoFlags,	Swap,		0,			0xff,	NoFlags },
		{ 0x7e,		NoFlags,	Swap,		0,			0xe7,	NoFlags },
		{ 0x01,		CarryFlag,	Swap,		0,			0x10,	NoFlags },
		{ 0x96,		CarryFlag,	Swap,		0,			0x69,	NoFlags },

		{ 0x0,		CarryFlag,	Srl,		0,			0x0,	ZeroFlag },
		{ 0xff,		NoFlags,	Srl,		0,			0x7f,	CarryFlag },
		{ 0xfe,		NoFlags,	Srl,		0,			0x7f,	NoFlags },
		{ 0x01,		CarryFlag,	Srl,		0,			0x0,	ZeroFlag | CarryFlag },
		{ 0xb2,		CarryFlag,	Srl,		0,			0x59,	NoFlags },

		{ 0x0,		CarryFlag,	Bit,		0,			0x0,	ZeroFlag | HalfCarryFlag | CarryFlag },
		{ 0x0,		NoFlags,	Bit,		1,			0x0,	ZeroFlag | HalfCarryFlag },
		{ 0x0,		CarryFlag,	Bit,		2,			0x0,	ZeroFlag | HalfCarryFlag | CarryFlag },
		{ 0x0,		NoFlags,	Bit,		3,			0x0,	ZeroFlag | HalfCarryFlag },
		{ 0x0,		CarryFlag,	Bit,		4,			0x0,	ZeroFlag | HalfCarryFlag | CarryFlag },
		{ 0x0,		CarryFlag,	Bit,		5,			0x0,	ZeroFlag | HalfCarryFlag | CarryFlag },
		{ 0x0,		NoFlags,	Bit,		6,			0x0,	ZeroFlag | HalfCarryFlag },
		{ 0x0,		CarryFlag,	Bit,		7,			0x0,	ZeroFlag | HalfCarryFlag | CarryFlag },

		{ 0xff,		NoFlags,	Bit,		0,			0xff,	HalfCarryFlag },
		{ 0xff,		CarryFlag,	Bit,		1,			0xff,	HalfCarryFlag | CarryFlag },
		{ 0xff,		CarryFlag,	Bit,		2,			0xff,	HalfCarryFlag | CarryFlag },
		{ 0xff,		CarryFlag,	Bit,		3,			0xff,	HalfCarryFlag | CarryFlag },
		{ 0xff,		NoFlags,	Bit,		4,			0xff,	HalfCarryFlag },
		{ 0xff,		NoFlags,	Bit,		5,			0xff,	HalfCarryFlag },
		{ 0xff,		CarryFlag,	Bit,		6,			0xff,	HalfCarryFlag | CarryFlag },
		{ 0xff,		NoFlags,	Bit,		7,			0xff,	HalfCarryFlag },

		{ 0xa6,		CarryFlag,	Bit,		0,			0xa6,	ZeroFlag | HalfCarryFlag | CarryFlag },
		{ 0xa6,		CarryFlag,	Bit,		1,			0xa6,	HalfCarryFlag | CarryFlag },
		{ 0xa6,		NoFlags,	Bit,		2,			0xa6,	HalfCarryFlag },
		{ 0xa6,		NoFlags,	Bit,		3,			0xa6,	ZeroFlag | HalfCarryFlag },
		{ 0xa6,		NoFlags,	Bit,		4,			0xa6,	ZeroFlag | HalfCarryFlag },
		{ 0xa6,		NoFlags,	Bit,		5,			0xa6,	HalfCarryFlag },
		{ 0xa6,		CarryFlag,	Bit,		6,			0xa6,	ZeroFlag | HalfCarryFlag | CarryFlag },
		{ 0xa6,		CarryFlag,	Bit,		7,			0xa6,	HalfCarryFlag | CarryFlag },

		// Input	Flags in				Operation	[Bit no.]	Result	Flags out
		{ 0x0,		AllFlags,				 Set,		0,			0x01,	AllFlags },
		{ 0x0,		NoFlags,				 Set,		1,			0x02,	NoFlags },
		{ 0x0,		CarryFlag,				 Set,		2,			0x04,	CarryFlag },
		{ 0x0,		HalfCarryFlag | SubFlag, Set,		3,			0x08,	HalfCarryFlag | SubFlag },
		{ 0x0,		SubFlag | CarryFlag,	 Set,		4,			0x10,	SubFlag | CarryFlag },
		{ 0x0,		ZeroFlag,				 Set,		5,			0x20,	ZeroFlag },
		{ 0x0,		NoFlags,				 Set,		6,			0x40,	NoFlags },
		{ 0x0,		AllFlags,				 Set,		7,			0x80,	AllFlags },

		{ 0x3a,		AllFlags,				 Set,		2,			0x3e,	AllFlags },
		{ 0x3a,		NoFlags,				 Set,		3,			0x3a,	NoFlags },
		{ 0x3a,		ZeroFlag | SubFlag,		 Set,		4,			0x3a,	ZeroFlag | SubFlag },
		{ 0x3a,		NoFlags,				 Set,		7,			0xba,	NoFlags },
		{ 0xdf,		NoFlags,				 Set,		5,			0xff,	NoFlags },

		{ 0xff,		AllFlags,				 Res,		0,			0xfe,	AllFlags },
		{ 0xff,		NoFlags,				 Res,		1,			0xfd,	NoFlags },
		{ 0xff,		CarryFlag,				 Res,		2,			0xfb,	CarryFlag },
		{ 0xff,		HalfCarryFlag | SubFlag, Res,		3,			0xf7,	HalfCarryFlag | SubFlag },
		{ 0xff,		SubFlag | CarryFlag,	 Res,		4,			0xef,	SubFlag | CarryFlag },
		{ 0xff,		ZeroFlag,				 Res,		5,			0xdf,	ZeroFlag },
		{ 0xff,		NoFlags,				 Res,		6,			0xbf,	NoFlags },
		{ 0xff,		AllFlags,				 Res,		7,			0x7f,	AllFlags },

		{ 0x3a,		AllFlags,				 Res,		2,			0x3a,	AllFlags },
		{ 0x3a,		NoFlags,				 Res,		3,			0x32,	NoFlags },
		{ 0x3a,		ZeroFlag | SubFlag,		 Res,		4,			0x2a,	ZeroFlag | SubFlag },
		{ 0x3a,		NoFlags,				 Res,		7,			0x3a,	NoFlags },
		{ 0xdf,		NoFlags,				 Res,		1,			0xdd,	NoFlags },

	};

	for (unsigned i = 0; i<sources.size(); i++)
	{
		for (auto& test : tests)
		{
			// Apply operand source and bit offset to base operation to get final opcode
			auto opcode = static_cast<unsigned char>(test[2] + (test[3] << 3) + i);

			MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xcb, opcode });

			auto& reg = Cpu.Registers();

			reg.PC = 0;
			reg.HL = MemoryMap::RamFixed;
			reg.F = test[1];
			std::get<2>(sources[i])(reg, test[0]);

			EXPECT_EQ(std::get<0>(sources[i]), Cpu.DoNextInstruction());

			EXPECT_EQ(test[4], std::get<1>(sources[i])(reg));
			EXPECT_EQ(test[5], reg.F);
			EXPECT_EQ(2, reg.PC);
		}
	}
}


TEST_F(CpuTestFixture, VBlankInterrupt)
{
	/*
	 * 0x0:					Start:
	 * 0xfa, 0x0,  0xc0		LD		A, (MemoryMap::RamFixed)
	 * 0xea, 0x1,  0xc0		ST		(MemoryMap::RamFixed + 1)
	 * 0xc3, 0x0,  0x0		JP		Start
	 * 
	 * 0x40:				VBlankInt:
	 * 0xe5					PUSH	HL
	 * 0x21, 0x0,  0xc0		LD		HL, MemoryMap::RamFixed
	 * 0x36, 0xa9			LD		(HL), 0xa9
	 * 0xe1					POP		HL
	 * 0xd9					RETI
	 * 
	 */

	// Main loop
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfa, 0x0, 0xc0, 0xea, 0x1, 0xc0, 0xc3, 0x0, 0x0 });

	// VBlank interrupt handler
	MemoryMap.SetBytes(0x40, { 0xe5, 0x21, 0x0, 0xc0, 0x36, 0xa9, 0xe1, 0xd9 });

	// Starting values
	MemoryMap.SetBytes(MemoryMap::RamFixed, { 0x38, 0xff, 0xff, 0xff });

	// Enable all individual interrupt types
	Cpu.EnabledInterrupts() = 0xff;

	auto& reg = Cpu.Registers();

	reg.PC = 0;
	reg.HL = 0x5678;
	reg.SP = MemoryMap::RamFixed + 6;
	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();

	EXPECT_EQ(0x38, MemoryMap.ReadByte(MemoryMap::RamFixed));
	EXPECT_EQ(0x38, MemoryMap.ReadByte(MemoryMap::RamFixed+1));

	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();

	// Should have jumped back to program start and done first instruction again
	EXPECT_EQ(0x3, reg.PC);

	Cpu.RequestInterrupt(InterruptFlags::VBlankInt);

	// Pushing PC and jumping to int vector takes five cycles
	EXPECT_EQ(FiveCycles, Cpu.DoNextInstruction());

	// Should be at int vector now
	EXPECT_EQ(0x40, reg.PC);

	// Should have pushed previous PC on stack
	EXPECT_EQ(reg.SP, MemoryMap::RamFixed + 4);
	EXPECT_EQ(0x3, MemoryMap.ReadByte(MemoryMap::RamFixed + 4));
	EXPECT_EQ(0x0, MemoryMap.ReadByte(MemoryMap::RamFixed + 5));

	// Interrupts should be disabled
	EXPECT_FALSE(Cpu.InterruptsEnabled());

	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	
	// HL should be MemoryMap::RamFixed
	EXPECT_EQ(0xc000, reg.HL);

	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();

	// HL should be restored
	EXPECT_EQ(0x5678, reg.HL);

	// Return from interrupt handler
	Cpu.DoNextInstruction();

	// Interrupts should be enabled
	EXPECT_TRUE(Cpu.InterruptsEnabled());

	// Should be back at instruction prior to interrupt
	EXPECT_EQ(0x3, reg.PC);

	// Should have popped PC from stack
	EXPECT_EQ(reg.SP, MemoryMap::RamFixed + 6);

	EXPECT_EQ(0xa9, MemoryMap.ReadByte(MemoryMap::RamFixed));

	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();
	Cpu.DoNextInstruction();

	// Main loop should have copied new value
	EXPECT_EQ(0xa9, MemoryMap.ReadByte(MemoryMap::RamFixed + 1));
}


TEST_F(CpuTestFixture, VBlankInterruptDisabled)
{
	/*
	* 0x0:					Start:
	* 0xfa, 0x0,  0xc0		LD		A, (MemoryMap::RamFixed)
	* 0xea, 0x1,  0xc0		ST		(MemoryMap::RamFixed + 1)
	* 0xc3, 0x0,  0x0		JP		Start
	* 
	*
	* 0x40:					VBlankInt:
	* 0xe5					PUSH	HL
	* 0x21, 0x0,  0xc0		LD		HL, MemoryMap::RamFixed
	* 0x36, 0xa9			LD		(HL), 0xa9
	* 0xe1					POP		HL
	* 0xd9					RETI
	*/

	// Main loop
	MemoryMap.SetBytes(MemoryMap::RomFixed, { 0xfa, 0x0, 0xc0, 0xea, 0x1, 0xc0, 0xc3, 0x0, 0x0 });

	// VBlank interrupt handler
	MemoryMap.SetBytes(0x40, { 0xe5, 0x21, 0x0, 0xc0, 0x36, 0xa9, 0xe1, 0xd9 });

	// Starting values
	MemoryMap.SetBytes(MemoryMap::RamFixed, { 0x38, 0xff, 0xff, 0xff });

	Cpu.EnabledInterrupts() = ~InterruptFlags::VBlankInt;

	auto& reg = Cpu.Registers();

	reg.PC = 0;
	reg.HL = 0x5678;
	reg.SP = MemoryMap::RamFixed + 6;

	// Run main loop for a bit
	for (auto i = 0; i < 10; i++) Cpu.DoNextInstruction();

	// Check that main loop is running as intended
	EXPECT_EQ(0x38, MemoryMap.ReadByte(MemoryMap::RamFixed));
	EXPECT_EQ(0x38, MemoryMap.ReadByte(MemoryMap::RamFixed + 1));

	Cpu.RequestInterrupt(InterruptFlags::VBlankInt);
	EXPECT_EQ(InterruptFlags::VBlankInt, Cpu.WaitingInterrupts());

	// Run main loop for a bit
	for (auto i = 0; i < 10; i++) Cpu.DoNextInstruction();

	// Check that interrupt hasn't triggered
	EXPECT_EQ(0x38, MemoryMap.ReadByte(MemoryMap::RamFixed));
	EXPECT_EQ(0x38, MemoryMap.ReadByte(MemoryMap::RamFixed + 1));
}



