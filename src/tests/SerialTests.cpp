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
	Serial SerialPort;
	std::vector<unsigned char> Sent;

	SerialTests()
	{
		SerialPort.SetCpu(&Processor);
		SerialPort.SetByteSentHandler([this](unsigned char byte) { Sent.push_back(byte); });
		Processor.WaitingInterrupts() = 0;

		// Transfers start at the beginning of a bit period unless a test says otherwise
		SerialPort.RunCycles(Serial::CyclesPerBit - Serial::PowerOnClocks);
	}

	void StartTransfer(unsigned char data, unsigned char control = 0x81)
	{
		SerialPort.WriteRegister(Serial::DataReg, data);
		SerialPort.WriteRegister(Serial::ControlReg, control);
	}

	bool SerialInterruptRequested() { return (Processor.WaitingInterrupts() & InterruptFlags::SerialInt) != 0; }
};

TEST_F(SerialTests, InternallyClockedTransferCompletesAfterEightBits)
{
	StartTransfer(0x42);

	SerialPort.RunCycles(TransferCycles - 1);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
	EXPECT_EQ(0xff, SerialPort.ReadRegister(Serial::ControlReg));

	SerialPort.RunCycles(1);
	ASSERT_EQ(1u, Sent.size());
	EXPECT_EQ(0x42, Sent[0]);
	EXPECT_TRUE(SerialInterruptRequested());
	EXPECT_EQ(0x7f, SerialPort.ReadRegister(Serial::ControlReg));
}

TEST_F(SerialTests, TransferStartedMidPeriodShiftsItsFirstBitAtThePeriodEnd)
{
	const int clocksIntoPeriod = 100;
	SerialPort.RunCycles(clocksIntoPeriod);
	StartTransfer(0x42);

	SerialPort.RunCycles(TransferCycles - clocksIntoPeriod - 1);
	EXPECT_TRUE(Sent.empty());

	SerialPort.RunCycles(1);
	EXPECT_EQ(1u, Sent.size());
}

TEST_F(SerialTests, ShiftsOutTopBitFirstAndShiftsInOnes)
{
	StartTransfer(0x42);

	SerialPort.RunCycles(512 * 3);
	EXPECT_EQ(0x17, SerialPort.ReadRegister(Serial::DataReg));

	SerialPort.RunCycles(512 * 5);
	EXPECT_EQ(0xff, SerialPort.ReadRegister(Serial::DataReg));
}

TEST_F(SerialTests, CompletesWhenRunInOneLargeStep)
{
	StartTransfer(0x99);

	SerialPort.RunCycles(TransferCycles * 3);
	ASSERT_EQ(1u, Sent.size());
	EXPECT_EQ(0x99, Sent[0]);
}

TEST_F(SerialTests, ExternallyClockedTransferNeverCompletes)
{
	StartTransfer(0x42, 0x80);

	SerialPort.RunCycles(TransferCycles * 10);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
	EXPECT_EQ(0x42, SerialPort.ReadRegister(Serial::DataReg));
	EXPECT_EQ(0xfe, SerialPort.ReadRegister(Serial::ControlReg));
}

TEST_F(SerialTests, NoTransferWithoutStartBit)
{
	StartTransfer(0x42, 0x01);

	SerialPort.RunCycles(TransferCycles);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
}

TEST_F(SerialTests, ClearingStartBitCancelsTransfer)
{
	StartTransfer(0x42);
	SerialPort.RunCycles(TransferCycles / 2);

	SerialPort.WriteRegister(Serial::ControlReg, 0x01);
	SerialPort.RunCycles(TransferCycles);
	EXPECT_TRUE(Sent.empty());
	EXPECT_FALSE(SerialInterruptRequested());
}

TEST_F(SerialTests, WritingStartBitDuringTransferRestartsIt)
{
	// Pan Docs doesn't say what this does on hardware. Emu Boy restarts the transfer with SB as it is, partly
	// shifted
	StartTransfer(0x42);
	SerialPort.RunCycles(TransferCycles / 2);

	SerialPort.WriteRegister(Serial::ControlReg, 0x81);
	SerialPort.RunCycles(TransferCycles - 1);
	EXPECT_TRUE(Sent.empty());

	SerialPort.RunCycles(1);
	ASSERT_EQ(1u, Sent.size());
	EXPECT_EQ(0x2f, Sent[0]);
}

TEST_F(SerialTests, UnusedControlBitsReadAsOne)
{
	EXPECT_EQ(0x7e, SerialPort.ReadRegister(Serial::ControlReg));

	SerialPort.WriteRegister(Serial::ControlReg, 0x01);
	EXPECT_EQ(0x7f, SerialPort.ReadRegister(Serial::ControlReg));
}

TEST_F(SerialTests, ConsecutiveTransfersAreEachReported)
{
	for (unsigned char byte : { 'O', 'K' })
	{
		StartTransfer(byte);
		SerialPort.RunCycles(TransferCycles);
	}

	EXPECT_EQ((std::vector<unsigned char>{ 'O', 'K' }), Sent);
}

TEST_F(SerialTests, MemoryMapRoutesSerialRegisters)
{
	Memory.SetSerial(&SerialPort);
	Memory.WriteByte(0xff01, 0x5a);
	Memory.WriteByte(0xff02, 0x81);

	EXPECT_EQ(0x5a, Memory.ReadByte(0xff01));
	EXPECT_EQ(0xff, Memory.ReadByte(0xff02));
}
