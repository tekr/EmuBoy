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
