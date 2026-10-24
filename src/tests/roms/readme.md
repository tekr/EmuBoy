Test ROMs
=========

`dmg_sound/` holds the single-test ROMs from Shay Green's (blargg's) Game Boy sound hardware tests, with
his readme. They come from https://github.com/retrio/gb-test-roms (originally
http://blargg.8bitalley.com/parodius/gb-tests/). `BlarggSoundTests.cpp` runs each one and reads the result
it writes to cartridge RAM at $A000.
