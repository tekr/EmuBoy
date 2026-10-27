# Possible gaps

Places where Emu Boy may differ from the original Game Boy, but where the correct behaviour is unclear or the
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
