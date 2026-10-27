
Emu Boy
=======

A Nintendo Game Boy emulator written in C++.

## Why another emulator?
There are many PC-based emulators that have existed for some time including [VBA](http://vba-m.com/) and [BGB](http://bgb.bircd.org/). This project was intentionally started independently as a hobby/practice exercise.

## Acknowledgments
Emu Boy has been built using Game Boy hardware information available online, primarily:

[Game Boy CPU manual](http://marc.rawer.de/Gameboy/Docs/GBCPUman.pdf)

[CPU opcodes](http://pastraiser.com/cpu/gameboy/gameboy_opcodes.html)

[Pan Docs](http://bgb.bircd.org/pandocs.htm)

[GB boot ROM](http://gbdev.gg8.se/wiki/articles/Gameboy_Bootstrap_ROM)

The repo includes a binary of [GBSnake](https://github.com/brovador/GBsnake).
    

## Dependencies
Emu Boy uses [SFML](http://www.sfml-dev.org) for outputting video & audio, and [Google Test](https://github.com/google/googletest) for unit testing.


## Building on macOS
Install the dependencies with [Homebrew](https://brew.sh), then build with CMake:

```sh
brew install cmake boost sfml@2 googletest
cmake -S . -B build -DSFML_DIR="$(brew --prefix sfml@2)/lib/cmake/SFML" -DCMAKE_PREFIX_PATH="$(brew --prefix boost);$(brew --prefix googletest)"
cmake --build build
./build/EmuBoy ROMs/gb-snake.gb
```

Run the unit tests with `ctest --test-dir build` (or run `../../build/Tests` from `src/tests`). Google Test is optional;
without it, only the emulator is built.

`EmuBoy [rom] [record-file] [scale]`: `scale` sets the window size as a multiple of the Game Boy's 160x144 screen
(default 4). If `record-file` is given, every frame is appended to it as raw 160x144 RGBA, which is useful for making
screen captures.


## Building on Linux
Install the dependencies with your package manager, then build with CMake. On Ubuntu 24.04 or Debian 12/13:

```sh
sudo apt-get update
sudo apt-get install cmake g++ libboost-dev libsfml-dev libgtest-dev
cmake -S . -B build
cmake --build build
./build/EmuBoy ROMs/gb-snake.gb
```

SFML 2.5 or 2.6 is needed; SFML 3 (the version packaged from Ubuntu 26.04) isn't supported yet. Run the unit
tests as on macOS.


## Current status
As of now Emu Boy is still in its early stages and is very much a work in progress. It currently supports the following:

* Fairly cycle-accurate emulation of the LR35902 CPU (all documented instructions + interrupts), including STOP
  and the lock-up caused by the unused opcodes
* Full graphics implementation (sprites + background + window support)
* Runs (at least) the included snake and Super Mario Land
* Sound: all four channels of the original Game Boy's sound hardware, including its documented quirks. Passes all of
  blargg's dmg_sound test ROMs
* Unit testing of most CPU instructions and the sound hardware
* Windows (Visual Studio 2015 solution), macOS and Linux (CMake), with CI builds and tests on macOS and Linux
* Simulates the slow response-time of the Game Boy LCD screen and smoothes upsized jagged pixels
* Keyboard input

&nbsp;
<p align="center" style="border: 5px solid red"><kbd><img src="http://codingthemachine.com/wp-content/uploads/2017/01/EmuBoyRun.gif" /></kbd></p>

## To-do list
Various features remain to be added including:

* Menus & ROM file selector
* Performance optimisation

Known bugs are tracked as [GitHub issues](https://github.com/tekr/EmuBoy/issues). Places where the emulator may
differ from the hardware, but where the right behaviour is unclear or unlikely to matter, are listed in
[docs/known-gaps.md](docs/known-gaps.md).
