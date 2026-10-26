
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

Save states snapshot the whole Game Boy (CPU, memory, graphics, sound, timer, serial port, the cartridge's mapper,
RAM and clock) so a game can carry on from that moment. There are four slots per game, kept next to the ROM as
`<name>.ss1` to `<name>.ss4` (`Tetris.gb` saves slot 1 to `Tetris.ss1`). F5 saves to the selected slot and F8 loads
from it; F6 and F7 select the previous and next slot. The window title briefly shows the slot selected and whether
saving or loading worked, and the terminal shows the same messages with any error. A state only loads into the game
it was saved from (it checks a checksum of the whole ROM) and into the same version of the state format; a state
that doesn't match, or is damaged, is reported and the game carries on undisturbed. Saving and loading take well
under a millisecond, and a state takes about 40 KB plus the cartridge's RAM.

Loading a state also puts back the cartridge RAM as it was when the state was saved, and that becomes the game's
battery save too: it is written to the `.sav` file within a few seconds, as if the game had just saved. This keeps
the `.sav` in line with what the game sees, rather than mixing a state's RAM with the newer save. So loading an
older state also takes the game's own save back to that point. So that a state loaded by mistake can be undone,
each time a state is loaded the game's RAM is first saved to the `.sav` (it is otherwise written every few seconds),
and the `.sav` is then copied to `<name>.sav.before-state`, so it always holds the save from just before the most
recent load. If that can't be done, the state isn't loaded. `<name>.sav.bak` is only ever the copy of a wrong-size
save. The real-time clock is restored as it was in the state, without catching up with the time since.

## Controls

| Game Boy | Keyboard | Gamepad |
|----------|----------|---------|
| D-pad    | W A S D  | D-pad or left stick |
| A        | `.`      | Right face button (B on Xbox pads) |
| B        | `,`      | Bottom face button (A on Xbox pads) |
| Start    | Return   | Start / Menu |
| Select   | Tab      | Back / View |

| Emulator | Key |
|----------|-----|
| Pause / resume | P |
| Fast-forward (up to 8x) | Hold Space |
| Reset (power off and on, keeping the save) | F2 |
| Save state to the selected slot | F5 |
| Select the previous / next save state slot (1-4) | F6 / F7 |
| Load state from the selected slot | F8 |
| Fullscreen on / off | F11; Ctrl+Cmd+F on a Mac (Escape also leaves fullscreen) |
| Screenshot | F12 |

The window title shows the game's title from the cartridge header, and whether it is paused or fast-forwarding.
Fast-forward is silent. Screenshots are saved at the Game Boy's 160x144 resolution
next to the ROM, named after it and numbered (`Tetris-1.png`, `Tetris-2.png`, ...), or in the working directory if the
ROM's folder can't be written to. On a Mac keyboard whose top row controls brightness and volume, hold fn for F2,
F5-F8 and F12. The emulator's commands are on the keyboard only.

Gamepad buttons are numbered differently by each platform's drivers. The defaults suit Xbox-style pads: through
DirectInput on Windows and the xpad driver on Linux (buttons 0, 1, 6 and 7), and Xbox One and Series pads over
Bluetooth on macOS (buttons 0, 1, 10 and 11). Other pads may number their buttons differently. On macOS, SFML 2
doesn't read d-pads that report as a hat switch numbered 1-8, as some Xbox pads' do, so use the left stick there.


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

* Fairly cycle-accurate emulation of the LR35902 CPU (all documented instructions + interrupts)
* Full graphics implementation (sprites + background + window support), with the LCD's timing, STAT interrupts,
  OAM DMA and the CPU's access to video memory accurate to the machine cycle, and the DMG's OAM corruption bug.
  Passes dmg-acid2, blargg's CPU, timing and oam_bug test ROMs, and the mooneye-test-suite acceptance tests that
  apply to the DMG
* Runs (at least) the included snake and Super Mario Land
* Sound: all four channels of the original Game Boy's sound hardware, including its documented quirks. Passes all of
  blargg's dmg_sound test ROMs
* Cartridge mappers: ROM only, MBC1 (including MBC1M multicarts), MBC2, MBC3 (including its real-time clock) and
  MBC5 (including rumble cartridges), chosen from the cartridge header, which covers almost all of the original Game
  Boy's library. Other mappers are run as MBC1 with a warning. Passes the mooneye-test-suite's MBC1, MBC2 and MBC5
  test ROMs and rtc3test
* Battery-backed saves, including the MBC3 real-time clock, compatible with other emulators' `.sav` files
* Save states in four slots per game
* Unit testing of most CPU instructions, the sound hardware and the cartridge mappers
* Windows (Visual Studio 2015 solution), macOS and Linux (CMake), with CI builds and tests on macOS and Linux
* Simulates the slow response-time of the Game Boy LCD screen and smoothes upsized jagged pixels
* Keyboard and gamepad input, pause, fast-forward, reset, save states, screenshots and fullscreen

&nbsp;
<p align="center" style="border: 5px solid red"><kbd><img src="http://codingthemachine.com/wp-content/uploads/2017/01/EmuBoyRun.gif" /></kbd></p>

## To-do list
Various features remain to be added including:

* Menus & ROM file selector
* Performance optimisation
