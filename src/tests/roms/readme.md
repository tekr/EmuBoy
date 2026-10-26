Test ROMs
=========

These are Shay Green's (blargg's) Game Boy hardware test ROMs, with his readmes. They come from
https://github.com/retrio/gb-test-roms (originally http://blargg.8bitalley.com/parodius/gb-tests/).
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
| `oam_bug/`      | OAM corruption by 16-bit increments and decrements       | `BlarggCpuTests.cpp`    |
