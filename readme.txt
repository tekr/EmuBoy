Emu Boy
=======
A Nintendo Game Boy emulator written in C++.

This project was started at the beginning of 2017 to improve my familiarity with the language.


## Why a Game Boy emulator?
I wanted to pick a project that wasn't biting off too much (given limited free time), but still offered ample opportunity to learn some low-level ins and outs of the language. Emulating console hardware seemed like a reasonable choice for the latter, and picking the Game Boy (4-'colour' screen, 8-bit Z80-like CPU, simple sprite graphics) satisfied the former.

There are many PC-based emulators that have existed for some time including ![VBA](http://vba-m.com/) and ![BGB](http://bgb.bircd.org/). I've intentionally (for now!) avoided looking at any of them given this is a learning exercise.

As such, Emu Boy is likely to contain non-idiomatic/suboptimal code and be far from state-of-the art in implementation, though I hope to change that over time.


## Acknowledgments
Emu Boy has been built solely from Game Boy hardware information available online; particularly the following sources:

![Game Boy CPU manual](http://marc.rawer.de/Gameboy/Docs/GBCPUman.pdf)
![CPU opcodes](http://pastraiser.com/cpu/gameboy/gameboy_opcodes.html)
![Pan Docs](http://bgb.bircd.org/pandocs.htm)
![GB boot ROM (read directly from the silicon with a microscope!)](http://gbdev.gg8.se/wiki/articles/Gameboy_Bootstrap_ROM)

The repo includes a binary of ![GBSnake](https://github.com/brovador/GBsnake).


## Dependencies
Emu Boy uses ~[http://www.sfml-dev.org/index.php](SFML) for outputting video & audio and Google Test for unit testing


## Current status
As of 2017-01-17 Emu Boy is still in its early stages and is very much a work in progress. It currently supports the following:

* Cycle-accurate emulation of the LR35902 CPU (all documented instructions + interrupts)
* Partial graphics implementation (background + window support)
* Successfully executes the internal Gameboy boot ROM, which scrolls the Nintendo logo and verifies the cartridge checksum on startup
* Partially runs the included snake ROM (graphics are incomplete and no input supported)
* Unit testing of most CPU instructions
* Windows-only (Visual Studio 2015 solution)
* Simulates the slow response-time of the Game Boy LCD screen and smoothes upsized jagged pixels


## To-do list
Plenty of features remain to be added including:

* Sprites
* Controller input
* DMA transfers
* Sound
* Linux/Mac OSX support


