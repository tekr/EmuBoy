#include "stdafx.h"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "../core/StateStream.h"

// StateWriter and StateReader: the fixed-width little-endian values and tagged sections save states are made of

namespace
{
	using Bytes = std::vector<unsigned char>;

	// Asserts that reading throws StateError with a message containing the text
	template <typename Read>
	void ExpectStateError(Read read, const std::string& text)
	{
		try
		{
			read();
			ADD_FAILURE() << "No StateError thrown";
		}
		catch (const StateError& e)
		{
			EXPECT_NE(std::string::npos, std::string{ e.what() }.find(text)) << e.what();
		}
	}
}

TEST(StateWriterTest, WritesIntegersLittleEndian)
{
	StateWriter writer;
	writer.WriteU8(0x12);
	writer.WriteU16(0x3456);
	writer.WriteU32(0x789abcde);
	writer.WriteU64(0x0123456789abcdefull);

	EXPECT_EQ((Bytes{ 0x12, 0x56, 0x34, 0xde, 0xbc, 0x9a, 0x78, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01 }), writer.GetData());
}

TEST(StateWriterTest, WritesSignedIntegersAsTwosComplement)
{
	StateWriter writer;
	writer.WriteInt(-2);

	EXPECT_EQ((Bytes{ 0xfe, 0xff, 0xff, 0xff }), writer.GetData());
}

TEST(StateWriterTest, WritesFlagsAsOneByte)
{
	StateWriter writer;
	writer.WriteBool(true);
	writer.WriteBool(false);

	EXPECT_EQ((Bytes{ 1, 0 }), writer.GetData());
}

TEST(StateWriterTest, WritesDoublesAsTheirIeee754BitPattern)
{
	StateWriter writer;
	writer.WriteDouble(1.0);

	EXPECT_EQ((Bytes{ 0, 0, 0, 0, 0, 0, 0xf0, 0x3f }), writer.GetData());
}

TEST(StateWriterTest, SectionHasItsTagAndLength)
{
	StateWriter writer;
	writer.BeginSection("ABCD");
	writer.WriteU16(0x0201);
	writer.WriteU8(3);
	writer.EndSection();

	EXPECT_EQ((Bytes{ 'A', 'B', 'C', 'D', 3, 0, 0, 0, 1, 2, 3 }), writer.GetData());
}

TEST(StateWriterTest, NestedSectionsCountTheirContents)
{
	StateWriter writer;
	writer.BeginSection("OUTR");
	writer.WriteU8(1);
	writer.BeginSection("INNR");
	writer.WriteU8(2);
	writer.EndSection();
	writer.EndSection();

	EXPECT_EQ((Bytes{ 'O', 'U', 'T', 'R', 10, 0, 0, 0, 1, 'I', 'N', 'N', 'R', 1, 0, 0, 0, 2 }), writer.GetData());
}

TEST(StateWriterTest, TagsMustBeFourCharacters)
{
	StateWriter writer;

	EXPECT_THROW(writer.BeginSection("ABC"), std::logic_error);
	EXPECT_THROW(writer.BeginSection("ABCDE"), std::logic_error);
}

TEST(StateWriterTest, EndingASectionNotBegunIsAnError)
{
	StateWriter writer;

	EXPECT_THROW(writer.EndSection(), std::logic_error);
}

TEST(StateReaderTest, ReadsBackEverythingWritten)
{
	StateWriter writer;
	writer.BeginSection("TEST");
	writer.WriteU8(0xab);
	writer.WriteU16(0xcdef);
	writer.WriteU32(0xfedcba98);
	writer.WriteU64(0xffeeddccbbaa9988ull);
	writer.WriteInt(-123456);
	writer.WriteBool(true);
	writer.WriteDouble(-0.125);
	const unsigned char bytes[] = { 9, 8, 7 };
	writer.WriteBytes(bytes, sizeof(bytes));
	writer.EndSection();

	StateReader reader{ writer.GetData() };
	reader.BeginSection("TEST");
	EXPECT_EQ(0xab, reader.ReadU8());
	EXPECT_EQ(0xcdef, reader.ReadU16());
	EXPECT_EQ(0xfedcba98u, reader.ReadU32());
	EXPECT_EQ(0xffeeddccbbaa9988ull, reader.ReadU64());
	EXPECT_EQ(-123456, reader.ReadInt(-200000, 0));
	EXPECT_TRUE(reader.ReadBool());
	EXPECT_EQ(-0.125, reader.ReadDouble());
	unsigned char read[3];
	reader.ReadBytes(read, sizeof(read));
	EXPECT_EQ((Bytes{ 9, 8, 7 }), Bytes(read, read + 3));
	reader.EndSection();

	EXPECT_TRUE(reader.AtEnd());
}

TEST(StateReaderTest, ReadsEnumerationsInRange)
{
	enum class Colour { Red, Green, Blue };
	StateWriter writer;
	writer.WriteInt(2);
	writer.WriteInt(3);

	StateReader reader{ writer.GetData() };

	EXPECT_EQ(Colour::Blue, reader.ReadEnum(Colour::Blue));
	ExpectStateError([&] { reader.ReadEnum(Colour::Blue); }, "outside 0 to 2");
}

TEST(StateReaderTest, ReadingPastTheEndFails)
{
	Bytes data{ 1, 2, 3 };
	StateReader reader{ data };

	ExpectStateError([&] { reader.ReadU32(); }, "truncated");
}

TEST(StateReaderTest, ReadingPastASectionsEndFails)
{
	StateWriter writer;
	writer.BeginSection("SECT");
	writer.WriteU8(1);
	writer.EndSection();
	writer.WriteU8(2);

	StateReader reader{ writer.GetData() };
	reader.BeginSection("SECT");

	ExpectStateError([&] { reader.ReadU16(); }, "runs past the section's end");
}

TEST(StateReaderTest, EndingASectionBeforeReadingAllOfItFails)
{
	StateWriter writer;
	writer.BeginSection("SECT");
	writer.WriteU16(1);
	writer.EndSection();

	StateReader reader{ writer.GetData() };
	reader.BeginSection("SECT");
	reader.ReadU8();

	ExpectStateError([&] { reader.EndSection(); }, "more than was read");
}

TEST(StateReaderTest, SectionWithAnotherTagFails)
{
	StateWriter writer;
	writer.BeginSection("SECT");
	writer.EndSection();

	StateReader reader{ writer.GetData() };

	ExpectStateError([&] { reader.BeginSection("OTHR"); }, "expected the OTHR section");
}

TEST(StateReaderTest, SectionLongerThanTheDataFails)
{
	Bytes data{ 'S', 'E', 'C', 'T', 5, 0, 0, 0, 1, 2 };
	StateReader reader{ data };

	ExpectStateError([&] { reader.BeginSection("SECT"); }, "runs past the end");
}

TEST(StateReaderTest, InnerSectionLongerThanItsOuterSectionFails)
{
	Bytes data{ 'O', 'U', 'T', 'R', 9, 0, 0, 0, 'I', 'N', 'N', 'R', 2, 0, 0, 0, 1, 2 };
	StateReader reader{ data };
	reader.BeginSection("OUTR");

	ExpectStateError([&] { reader.BeginSection("INNR"); }, "runs past the end");
}

TEST(StateReaderTest, ErrorsNameTheSection)
{
	Bytes data{ 'C', 'P', 'U', ' ', 0, 0, 0, 0 };
	StateReader reader{ data };
	reader.BeginSection("CPU ");

	ExpectStateError([&] { reader.ReadU8(); }, "in its CPU section");
}

TEST(StateReaderTest, IntegerOutsideTheAllowedRangeFails)
{
	StateWriter writer;
	writer.WriteInt(10);
	writer.WriteInt(-1);

	StateReader reader{ writer.GetData() };

	ExpectStateError([&] { reader.ReadInt(0, 9); }, "10, outside 0 to 9");
	ExpectStateError([&] { reader.ReadInt(0, 9); }, "-1, outside 0 to 9");
}

TEST(StateReaderTest, FlagOtherThanZeroOrOneFails)
{
	Bytes data{ 2 };
	StateReader reader{ data };

	ExpectStateError([&] { reader.ReadBool(); }, "flag is 2");
}

TEST(StateReaderTest, DoubleThatIsntFiniteFails)
{
	StateWriter writer;
	writer.WriteDouble(std::numeric_limits<double>::quiet_NaN());
	writer.WriteDouble(std::numeric_limits<double>::infinity());

	StateReader reader{ writer.GetData() };

	ExpectStateError([&] { reader.ReadDouble(); }, "not a number");
	ExpectStateError([&] { reader.ReadDouble(); }, "not a number");
}

TEST(StateReaderTest, FailReportsTheCallersProblemInTheSection)
{
	Bytes data{ 'M', 'B', 'C', '1', 0, 0, 0, 0 };
	StateReader reader{ data };
	reader.BeginSection("MBC1");

	ExpectStateError([&] { reader.Fail("something is wrong"); }, "invalid in its MBC1 section: something is wrong");
}
