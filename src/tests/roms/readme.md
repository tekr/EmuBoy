Test ROMs
=========

blargg
------

The directories in the table are Shay Green's (blargg's) Game Boy hardware test ROMs, with his readmes. They come
from https://github.com/retrio/gb-test-roms (originally http://blargg.8bitalley.com/parodius/gb-tests/).
`BlarggRomRunner.cpp` runs a ROM and reads the result it reports through the serial port or cartridge RAM
at $A000.

| Directory       | Tests                                                    | Run by                  |
|-----------------|----------------------------------------------------------|-------------------------|
| `dmg_sound/`    | Sound hardware (the single-test ROMs)                    | `BlarggSoundTests.cpp`  |
| `cpu_instrs/`   | CPU instruction results (the individual ROMs)            | `BlarggCpuTests.cpp`    |
| `instr_timing/` | Instruction timing                                       | `BlarggCpuTests.cpp`    |
| `mem_timing/`   | When instructions read and write memory                  | `BlarggCpuTests.cpp`    |
| `mem_timing-2/` | The same, from a later version of the test shell         | `BlarggCpuTests.cpp`    |
| `halt_bug/`     | The HALT bug                                             | `BlarggCpuTests.cpp`    |
| `oam_bug/`      | OAM corruption by 16-bit increments and decrements       | `BlarggOamBugTests.cpp` |

`oam_bug/7-timing_effect.gb` isn't run. It logs an OAM dump for each of the 20 timings in its range that corrupt OAM,
over 8 KB of text, more than the cartridge RAM it logs to holds, so the log runs on into the test's own code in work
RAM, which starts at $C000 straight after cartridge RAM, and it crashes. In a scratch build that kept its code from
being overwritten, it passed the checksum it takes of that output.

`interrupt_time` isn't included. It's a Game Boy Color test (its header marks it CGB-only and its source sets
`REQUIRE_CGB`) that times interrupts at both CPU speeds, so it needs the CGB's double-speed mode.

dmg-acid2
---------

`dmg-acid2/` holds Matt Currie's dmg-acid2 PPU test (MIT licence, in `LICENSE`), from
https://github.com/mattcurrie/dmg-acid2. `dmg-acid2.gb` is the v1.0 release ROM.
`reference-dmg.txt` is the repository's reference image, `img/reference-dmg.png` (160x144, 2-bit greyscale),
converted to text so the tests need no image library: one line per screen row, one digit per pixel, giving
the DMG shade from 0 (lightest, PNG value $FF) to 3 (darkest, $00), i.e. `3 - value / 85` for each 8-bit
greyscale value. `DmgAcid2Tests.cpp` runs the ROM and compares the frame with it pixel for pixel.

mooneye-test-suite
------------------

`mooneye-test-suite/` holds test ROMs from Joonas Javanainen's (Gekkio's) mooneye-test-suite, with its licence,
from the release build https://gekkio.fi/files/mooneye-test-suite/mts-20260714-0944-31510e1/ (source:
https://github.com/Gekkio/mooneye-test-suite). `MooneyeMbcTests.cpp` runs the `emulator-only` MBC1, MBC2 and MBC5
ROMs with `MooneyeRomRunner.h`, which detects the result each ROM leaves in the CPU registers.

rtc3test
--------

`rtc3test/` holds rtc3test v004 by aaaaaa123456789, which tests the MBC3 real-time clock, with its licence (it is
released into the public domain under the Unlicense). It comes from
https://github.com/aaaaaa123456789/rtc3test/releases/tag/v004. `Rtc3Tests.cpp` runs each of its test suites through
the menu and reads the results it draws on screen with `ScreenText.h`.
