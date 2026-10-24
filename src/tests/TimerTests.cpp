#include "stdafx.h"
#include <gtest/gtest.h>
#include <new>
#include <cstring>
#include "../core/Timer.h"

TEST(TimerTest, RegistersStartAtZero)
{
	// Construct over non-zero memory so leftover values would show
	alignas(Timer) unsigned char storage[sizeof(Timer)];
	memset(storage, 0xa5, sizeof(storage));
	auto timer = new (storage) Timer();

	EXPECT_EQ(0, timer->ReadRegister(0));
	EXPECT_EQ(0, timer->ReadRegister(1));
	EXPECT_EQ(0, timer->ReadRegister(2));

	timer->~Timer();
}
