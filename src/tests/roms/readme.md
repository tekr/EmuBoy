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
