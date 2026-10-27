
Emu Boy
=======
[![CI](https://github.com/tekr/EmuBoy/actions/workflows/ci.yml/badge.svg)](https://github.com/tekr/EmuBoy/actions/workflows/ci.yml)

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

Games with battery-backed RAM keep their saves in a `.sav` file next to the ROM, named after it (`Tetris.gb` saves
to `Tetris.sav`). It is loaded at start-up, written within a few seconds of the game changing it, and written on exit
if anything changed (always, for real-time clock cartridges, to record the time). On macOS, exiting includes closing
the window, Ctrl+C and closing the terminal, and save errors and warnings are shown in the terminal. On Windows,
saves are written when the window is closed; Emu Boy has no console there, so messages aren't shown. If a save is the
wrong size for the cartridge, what fits is loaded, and the original is copied to `<name>.sav.bak` before it is first
replaced.

The format is the raw RAM dump most emulators use, with the BGB/VBA-M real-time clock footer for MBC3 clock
cartridges, so saves can be moved to and from BGB, VBA-M, mGBA and SameBoy. A clock save from mGBA can run ahead by
the time between the game last reading the clock and mGBA saving. MBC2 saves are written as 512 bytes, as SameBoy and
VBA-M do; mGBA's 256-byte MBC2 saves can be loaded too. The clock catches up with the time that passed while Emu Boy
wasn't running.


## Building on Linux
Install the dependencies with your package manager, then build with CMake. On Ubuntu 24.04 or Debian 12/13:

```sh
sudo apt-get install cmake g++ libboost-dev libsfml-dev libgtest-dev
cmake -S . -B build
cmake --build build
./build/EmuBoy ROMs/gb-snake.gb
```

SFML 2.5 or 2.6 is needed; SFML 3 (the version packaged from Ubuntu 26.04) isn't supported yet. Run the unit
tests as on macOS.


## Current status
As of now Emu Boy is still in its early stages and is very much a work in progress. It currently supports the following:

* Fairly cycle-accurate emulation of the LR35902 CPU (all documented instructions + interrupts)
* Full graphics implementation (sprites + background + window support). The LCD's timing (including how long each
  line's mode 3 lasts, from the registers as mode 3 starts), STAT interrupts, OAM DMA, when the CPU can access video
  memory, and the DMG's OAM corruption bug are emulated to the machine cycle. The pixels aren't: each line is drawn
  in one go as its mode 3 starts, so a game changing the scroll, palette, LCDC or window registers partway through
  mode 3 doesn't affect that line, or how long its mode 3 lasts.
  Passes dmg-acid2, blargg's CPU, timing and oam_bug test ROMs, the mooneye-test-suite acceptance tests that apply
  to the DMG, and all but one of gbmicrotest's STAT write bug tests (see `src/tests/roms/readme.md`)
* Runs (at least) the included snake and Super Mario Land
* Sound: all four channels of the original Game Boy's sound hardware, including its documented quirks. Passes all of
  blargg's dmg_sound test ROMs
* Cartridge mappers: ROM only, MBC1 (including MBC1M multicarts), MBC2, MBC3 (including its real-time clock) and
  MBC5 (including rumble cartridges), chosen from the cartridge header, which covers almost all of the original Game
  Boy's library. Other mappers are run as MBC1 with a warning. Passes the mooneye-test-suite's MBC1, MBC2 and MBC5
  test ROMs and rtc3test
* Battery-backed saves, including the MBC3 real-time clock, compatible with other emulators' `.sav` files
* Unit testing of most CPU instructions, the sound hardware and the cartridge mappers
* Windows (Visual Studio 2015 solution), macOS and Linux (CMake), with CI builds and tests on macOS and Linux
* Simulates the slow response-time of the Game Boy LCD screen and smoothes upsized jagged pixels
* Keyboard input

&nbsp;
<p align="center" style="border: 5px solid red"><kbd><img src="http://codingthemachine.com/wp-content/uploads/2017/01/EmuBoyRun.gif" /></kbd></p>

## To-do list
Various features remain to be added including:

* Menus & ROM file selector
* Drawing pixels as mode 3 runs (a pixel FIFO), for effects that change registers partway through a line
* Performance optimisation
