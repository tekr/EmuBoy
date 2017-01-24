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


## Current status
As of now Emu Boy is still in its early stages and is very much a work in progress. It currently supports the following:

* Fairly cycle-accurate emulation of the LR35902 CPU (all documented instructions + interrupts)
* Partial graphics implementation (background + window support)
* Successfully executes the internal Gameboy boot ROM, which scrolls the Nintendo logo and verifies the cartridge checksum on startup
* Partially runs the included snake game ROM (graphics are incomplete and no input supported)
* Unit testing of most CPU instructions
* Windows-only (Visual Studio 2015 solution)
* Simulates the slow response-time of the Game Boy LCD screen and smoothes upsized jagged pixels

&nbsp;
<p align="center" style="border: 5px solid red"><kbd><img src="http://codingthemachine.com/wp-content/uploads/2017/01/EmuBoyRun.gif" /></kbd></p>

## To-do list
Plenty of features remain to be added including:

* Sprites
* Controller input
* DMA transfers
* Sound
* Linux/Mac OSX support
* Performance optimisation
