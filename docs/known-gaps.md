# Possible gaps

Places where Emu Boy may differ from the original Game Boy or the Game Boy Color, but where the correct behaviour is unclear or the
difference is unlikely to matter in practice. Confirmed bugs are tracked as GitHub issues instead:
[#29](https://github.com/tekr/EmuBoy/issues/29) (CPU timing), [#30](https://github.com/tekr/EmuBoy/issues/30) (timer)
and [#31](https://github.com/tekr/EmuBoy/issues/31) (graphics).

## Graphics
- **Whole-line rendering:** each line is drawn in one go at the end of the line, so effects that change graphics
  registers partway through a line aren't shown. This is a design choice that keeps rendering simple.
- **Sprite size changes mid-frame:** what the hardware draws when LCDC bit 2 changes between a line's sprite selection
  and its rendering isn't clearly documented. (The out-of-range read this can cause is a bug, in #31.)

## Sound
- **Noise frequency changes:** a write to NR43 without a retrigger takes effect only when the current noise period ends.
  This follows the gbdev wiki's model. SameBoy applies it immediately and is probably closer to hardware, but this
  hasn't been confirmed by a test ROM or a hardware measurement. It's only audible when the old period is long.
- **Envelope settings:** the envelope's period and direction are latched when the channel is triggered. SameBoy reads
  them live, but Pan Docs says such writes need a retrigger to take effect.
- **Wave RAM access while channel 3 plays:** access is allowed only on the exact clock when channel 3 reads wave RAM,
  rather than for the whole 2 MHz tick. CPU accesses fall on 4-clock boundaries, so software can't tell the difference.
- **Sub-instruction details:** SameBoy models some finer timing details, such as glitches on NR10 writes, that Emu Boy
  doesn't. Hardware behaviour here is only partly documented.

## Game Boy Color
- **No CGB boot ROM:** a CGB game starts in the state the boot ROM leaves, from Pan Docs. Some of it isn't modelled:
  the boot ROM's logo isn't left in VRAM, the object palettes (which it leaves uninitialised) are zero, DIV starts
  as on the DMG at power-on rather than at the count the boot ROM's run (which depends on the header) would leave,
  CH1's frequency is left at 0 though it's silent, and the LCD has just been turned on, so the first frame shows the
  LCD-off colour.
- **No DMG compatibility mode:** a game whose header doesn't ask for the CGB runs on the DMG, so the CGB's
  colourised mode for DMG games isn't emulated. For the same reason KEY0 always reads $FF, as it does once the boot
  ROM has locked it, and objects are always prioritised by their position in OAM: as in SameBoy and Gambatte, writes
  to OPRI after the boot ROM are kept for reading back but have no effect (Pan Docs agrees, though marks it to be
  verified).
- **Speed switch pause:** Gambatte's hardware tests, recorded on a CGB-04C, contradict Pan Docs: the pause lasts about
  0x20000 CPU clocks rather than 2050 machine cycles, and DIV, the timer and the APU keep running through it rather
  than DIV standing still (TIMA counts through it, and a sound's length can run out in it). Those tests fix DIV's
  restart at 0x20000 clocks before the CPU resumes, but not where in the pause it comes; the CPU resumes 0x20008 CPU
  clocks after STOP's opcode fetch, with DIV restarting 8 clocks in, as SameBoy's 0x20008 does, the values that pass
  the most of Gambatte's 206 speedchange tests that don't record audio (150). Interrupts wait for the pause to end,
  where SameBoy and Gambatte let one end it as it ends a HALT (and SameBoy skips the pause when one is pending).
- **Timing across a speed switch:** of those 206 tests, 56 still fail, by about a cycle:
  - 43 on the PPU's mode 3 STAT timing just after a switch (`*m3stat*`, `*m2int*`), mostly one of a pair a cycle
    apart. The PPU carries on as normal through the switch, where SameBoy notes the switch takes 2 extra T-cycles
    (2 PPU ticks going to double speed, 1 coming back), and Pan Docs says its access to VRAM and OAM is frozen as the
    switch found it.
  - 9 on CH2's length (`*ch2_nr52*a`), which runs out a little sooner than on the hardware.
  - 4 on TIMA (`speedchange_tima00_1a/_1b`, `speedchange2_tima00_1a/_1b`), one count high.
- **VRAM DMA:** the timing (a machine cycle to set up, then 2 bytes per microsecond) is SameBoy's. The destination
  wraps within VRAM and a transfer stops early only when the whole 16-bit address passes $FFFF, as Gambatte's hardware
  tests `dma_dst_wrap_1` and `dma_dst_wrap_2` show. Cancelling an H-Blank transfer leaves HDMA5 reading the blocks
  that were left, as Pan Docs says, against SameBoy and Gambatte, which load the length just written (so read $80
  after writing $00); no hardware test is known to settle it, and games test only bit 7, on which all three agree.
  Sources in VRAM or above $DFFF copy $FF rather than the garbage Pan Docs describes, and the bus conflicts SameBoy
  models aren't.
- **Palette access:** BCPD and OCPD are blocked for exactly mode 3; SameBoy's timing is a few clocks different.
- **Revision differences:** Emu Boy emulates CGB revision E, SameBoy's default model: the unusable area at
  $FEA0-$FEFF repeats the address's high nibble, and RP's bit 4 reads 0, so RP reads $2E at power-on rather than the
  $3E Pan Docs gives for earlier revisions. RP never receives infrared light. The APU's revision-specific details in
  SameBoy (such as PCM12/PCM34 masking on revisions up to C, and finer timing at double speed) aren't modelled.
- **Turning the LCD back on:** the first frame after the LCD is turned on shows the LCD-off colour, on the CGB as on the
  DMG. SameBoy shows the previous frame again instead on a CGB if the LCD comes back on within about 4 lines of the
  start of the last V-Blank; turned off for longer, as when loading VRAM between screens, SameBoy shows the LCD-off
  colour too.
- **Screen:** the colour correction follows Pokefan531's measured model at commit a21d4d4 of his
  Handheld-Colorspace-Shaders, whose default gamma ramp he marks as still being worked on. The 80% opacity the CGB's
  frames are drawn at, for its faster but not instant LCD, is a judgement rather than a measurement.

## Audio playback
- **Underruns:** when the audio buffer runs dry (mainly at start-up, or after the latency cap drops samples), playback is
  padded with silence rather than holding the last sample. The emulator's clock and the audio device's clock aren't
  kept in step. The drift measured was about 0.04 samples a second.
- **Unread samples:** samples accumulate if nothing calls `Emulator::TakeAudioSamples`. The app calls it every frame,
  so this only affects other code using the core.
- **Small details:** `main.cpp` checks the audio stream's status every frame, and the 1024-sample chunks add a little
  latency.

## Platforms
- **Windows:** the Visual Studio projects were updated for sound and the new tests, but haven't been built or run since.
  That includes the bundled `openal32.dll` and the test project's older Google Test 1.7.
