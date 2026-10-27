#include "stdafx.h"
#include <gtest/gtest.h>
#include <vector>
#include "CpuTestFixture.h"
#include "../core/Serial.h"

// Per https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html: writing SC with bits 7 and 0 set starts
// a transfer clocked internally at 8192 Hz (512 clocks a bit). With no link partner, 1s are shifted in.
// When all 8 bits have been shifted, bit 7 of SC is cleared and the serial interrupt is requested. The clock is
// divided down from power-on, so bits shift on its period boundaries (mooneye-test-suite's boot_sclk_align-dmgABCmgb).

const int TransferCycles = 8 * 512;

class SerialTests : public CpuTestFixture
{
public:
	// Members named after their types qualify the type with :: so the name's meaning doesn't change within the
	// class, which GCC rejects
	::Serial Serial;
	std::vector<unsigned char> Sent;

	SerialTests()
	{
		Serial.SetCpu(&Cpu);
		Serial.SetByteSentHandler([this](unsigned char byte) { Sent.push_back(byte); });
		Cpu.WaitingInterrupts() = 0;

		// Transfers start at the beginning of a bit period unless a test says otherwise
		Serial.RunCycles(Serial::CyclesPerBit - Serial::PowerOnClocks);
	}

	void StartTransfer(unsigned char data, unsigned char control = 0x81)
	{
		Serial.WriteRegister(Serial::DataReg, data);
		Serial.WriteRegister(Serial::ControlReg, control);
	}

	bool SerialInterruptRequested() { return (Cpu.WaitingInterrupts() & InterruptFlags::SerialInt) != 0; }
};

TEST_F(SerialTests, InternallyClockedTransferCompletesAfterEightBits)
{
	StartTransfer(0x42);

	Serial.RunCycles(TransferCycles - 1);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
	EXPECT_EQ(0xff, Serial.ReadRegister(Serial::ControlReg));

	Serial.RunCycles(1);
	ASSERT_EQ(1u, Sent.size());
	EXPECT_EQ(0x42, Sent[0]);
	EXPECT_TRUE(SerialInterruptRequested());
	EXPECT_EQ(0x7f, Serial.ReadRegister(Serial::ControlReg));
}

TEST_F(SerialTests, TransferStartedMidPeriodShiftsItsFirstBitAtThePeriodEnd)
{
	const int clocksIntoPeriod = 100;
	Serial.RunCycles(clocksIntoPeriod);
	StartTransfer(0x42);

	Serial.RunCycles(TransferCycles - clocksIntoPeriod - 1);
	EXPECT_TRUE(Sent.empty());

	Serial.RunCycles(1);
	EXPECT_EQ(1u, Sent.size());
}

TEST_F(SerialTests, ShiftsOutTopBitFirstAndShiftsInOnes)
{
	StartTransfer(0x42);

	Serial.RunCycles(512 * 3);
	EXPECT_EQ(0x17, Serial.ReadRegister(Serial::DataReg));

	Serial.RunCycles(512 * 5);
	EXPECT_EQ(0xff, Serial.ReadRegister(Serial::DataReg));
}

TEST_F(SerialTests, CompletesWhenRunInOneLargeStep)
{
	StartTransfer(0x99);

	Serial.RunCycles(TransferCycles * 3);
	ASSERT_EQ(1u, Sent.size());
	EXPECT_EQ(0x99, Sent[0]);
}

TEST_F(SerialTests, ExternallyClockedTransferNeverCompletes)
{
	StartTransfer(0x42, 0x80);

	Serial.RunCycles(TransferCycles * 10);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
	EXPECT_EQ(0x42, Serial.ReadRegister(Serial::DataReg));
	EXPECT_EQ(0xfe, Serial.ReadRegister(Serial::ControlReg));
}

TEST_F(SerialTests, NoTransferWithoutStartBit)
{
	StartTransfer(0x42, 0x01);

	Serial.RunCycles(TransferCycles);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
}

TEST_F(SerialTests, ClearingStartBitCancelsTransfer)
{
	StartTransfer(0x42);
	Serial.RunCycles(TransferCycles / 2);

	Serial.WriteRegister(Serial::ControlReg, 0x01);
	Serial.RunCycles(TransferCycles);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
}

TEST_F(SerialTests, WritingStartBitDuringTransferRestartsIt)
{
	// Pan Docs doesn't say what this does on hardware. Emu Boy restarts the transfer with SB as it is, partly
	// shifted
	StartTransfer(0x42);
	Serial.RunCycles(TransferCycles / 2);

	Serial.WriteRegister(Serial::ControlReg, 0x81);
	Serial.RunCycles(TransferCycles - 1);
	EXPECT_TRUE(Sent.empty());

	Serial.RunCycles(1);
	ASSERT_EQ(1u, Sent.size());
	EXPECT_EQ(0x2f, Sent[0]);
}

TEST_F(SerialTests, UnusedControlBitsReadAsOne)
{
	EXPECT_EQ(0x7e, Serial.ReadRegister(Serial::ControlReg));

	Serial.WriteRegister(Serial::ControlReg, 0x01);
	EXPECT_EQ(0x7f, Serial.ReadRegister(Serial::ControlReg));
}

TEST_F(SerialTests, ConsecutiveTransfersAreEachReported)
{
	for (unsigned char byte : { 'O', 'K' })
	{
		StartTransfer(byte);
		Serial.RunCycles(TransferCycles);
	}

	EXPECT_EQ((std::vector<unsigned char>{ 'O', 'K' }), Sent);
}

TEST_F(SerialTests, MemoryMapRoutesSerialRegisters)
{
	MemoryMap.SetSerial(&Serial);
	MemoryMap.WriteByte(0xff01, 0x5a);
	MemoryMap.WriteByte(0xff02, 0x81);

	EXPECT_EQ(0x5a, MemoryMap.ReadByte(0xff01));
	EXPECT_EQ(0xff, MemoryMap.ReadByte(0xff02));
}
