Test ROMs
=========

`dmg_sound/` holds the single-test ROMs from Shay Green's (blargg's) Game Boy sound hardware tests, with
his readme. They come from https://github.com/retrio/gb-test-roms (originally
http://blargg.8bitalley.com/parodius/gb-tests/). `BlarggSoundTests.cpp` runs each one and reads the result
it writes to cartridge RAM at $A000.

`dmg-acid2/` holds Matt Currie's dmg-acid2 PPU test (MIT licence, in `LICENSE`), from
https://github.com/mattcurrie/dmg-acid2. `dmg-acid2.gb` is the v1.0 release ROM.
`reference-dmg.txt` is the repository's reference image, `img/reference-dmg.png` (160x144, 2-bit greyscale),
converted to text so the tests need no image library: one line per screen row, one digit per pixel, giving
the DMG shade from 0 (lightest, PNG value $FF) to 3 (darkest, $00), i.e. `3 - value / 85` for each 8-bit
greyscale value. `DmgAcid2Tests.cpp` runs the ROM and compares the frame with it pixel for pixel.
