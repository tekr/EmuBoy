Test ROMs
=========

`dmg_sound/` holds the single-test ROMs from Shay Green's (blargg's) Game Boy sound hardware tests, with
his readme. They come from https://github.com/retrio/gb-test-roms (originally
http://blargg.8bitalley.com/parodius/gb-tests/). `BlarggSoundTests.cpp` runs each one and reads the result
it writes to cartridge RAM at $A000.

`mooneye-test-suite/` holds test ROMs from Joonas Javanainen's (Gekkio's) mooneye-test-suite, with its licence,
from the release build https://gekkio.fi/files/mooneye-test-suite/mts-20260714-0944-31510e1/ (source:
https://github.com/Gekkio/mooneye-test-suite). `MooneyeMbcTests.cpp` runs the `emulator-only` MBC1, MBC2 and MBC5
ROMs with `MooneyeRomRunner.h`, which detects the result each ROM leaves in the CPU registers.

`rtc3test/` holds rtc3test v004 by aaaaaa123456789, which tests the MBC3 real-time clock, with its licence (it is
released into the public domain under the Unlicense). It comes from
https://github.com/aaaaaa123456789/rtc3test/releases/tag/v004. `Rtc3Tests.cpp` runs each of its test suites through
the menu and reads the results it draws on screen with `ScreenText.h`.
