cmake_minimum_required(VERSION 3.16)
project(EmuBoy CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED)
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

file(GLOB CORE_SOURCES src/core/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX "stdafx\\.cpp$")
add_library(core STATIC ${CORE_SOURCES})
target_include_directories(core PUBLIC src/core)
target_link_libraries(core PUBLIC Boost::boost)

add_executable(EmuBoy src/ui/main.cpp)
target_link_libraries(EmuBoy PRIVATE core sfml-graphics sfml-window sfml-system)

# Unit tests (Google Test). Run with ctest, or run Tests from src/tests, which the tests' file paths are relative to
find_package(GTest)
if(GTest_FOUND)
	enable_testing()
	file(GLOB TEST_SOURCES src/tests/*.cpp)
	list(FILTER TEST_SOURCES EXCLUDE REGEX "stdafx\\.cpp$")
	add_executable(Tests ${TEST_SOURCES})
	target_link_libraries(Tests PRIVATE core GTest::gtest)
	add_test(NAME Tests COMMAND Tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/tests)
endif()
