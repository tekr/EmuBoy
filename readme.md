
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

Games with battery-backed RAM keep their saves in a `.sav` file next to the ROM, named after it (`Tetris.gb` saves
to `Tetris.sav`). It is loaded at start-up, written within a few seconds of the game changing it, and written on exit
if anything changed (always, for real-time clock cartridges, to record the time). On macOS, exiting includes closing
the window, Ctrl+C and closing the terminal, and save errors and warnings are shown in the terminal. On Windows,
saves are written when the window is closed; Emu Boy has no console there, so messages aren't shown. If a save is the
wrong size for the cartridge, what fits is loaded, and the original is copied to `<name>.sav.bak` before it is first
replaced (as it is for another emulator's save that loads fully but is written back at a different size).

The format is the raw RAM dump most emulators use, with the BGB/VBA-M real-time clock footer for MBC3 clock
cartridges, so saves can be moved to and from BGB, VBA-M, mGBA and SameBoy. A clock save from mGBA can run ahead by
the time between the game last reading the clock and mGBA saving. MBC2 saves are written as 512 bytes, as SameBoy and
VBA-M do; mGBA's 256-byte MBC2 saves can be loaded too. Cartridges with 2 KB of RAM save 2 KB, as BGB, SameBoy and
VBA-M do; mGBA's 8 KB saves for them load too, with a warning if the 6 KB beyond the cartridge's RAM holds data. The
clock catches up with the time that passed while Emu Boy wasn't running. Battery-backed cartridges whose mapper
isn't emulated (MMM01, Pocket Camera, HuC1 and HuC3) still have their RAM saved, though the HuC3's clock isn't, and
the MBC7's EEPROM isn't saved at all.


## Current status
As of now Emu Boy is still in its early stages and is very much a work in progress. It currently supports the following:

* Fairly cycle-accurate emulation of the LR35902 CPU (all documented instructions + interrupts)
* Full graphics implementation (sprites + background + window support)
* Runs (at least) the included snake and Super Mario Land
* Sound: all four channels of the original Game Boy's sound hardware, including its documented quirks. Passes all of
  blargg's dmg_sound test ROMs
* Cartridge mappers: ROM only, MBC1 (including MBC1M multicarts), MBC2, MBC3 (including its real-time clock) and
  MBC5 (including rumble cartridges), chosen from the cartridge header, which covers almost all of the original Game
  Boy's library. Other mappers are run as MBC1 with a warning. Passes the mooneye-test-suite's MBC1, MBC2 and MBC5
  test ROMs and rtc3test
* Battery-backed saves, including the MBC3 real-time clock, compatible with other emulators' `.sav` files
* Unit testing of most CPU instructions, the sound hardware and the cartridge mappers
* Windows (Visual Studio 2015 solution) and macOS (CMake)
* Simulates the slow response-time of the Game Boy LCD screen and smoothes upsized jagged pixels
* Keyboard input

&nbsp;
<p align="center" style="border: 5px solid red"><kbd><img src="http://codingthemachine.com/wp-content/uploads/2017/01/EmuBoyRun.gif" /></kbd></p>

## To-do list
Various features remain to be added including:

* Menus & ROM file selector
* Linux support
* Performance optimisation
