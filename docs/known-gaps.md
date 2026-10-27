# Possible gaps

Places where Emu Boy may differ from the original Game Boy, but where the correct behaviour is unclear or the
difference is unlikely to matter in practice. Confirmed bugs are tracked as
[GitHub issues](https://github.com/tekr/EmuBoy/issues) instead.

## CPU
- **STOP:** follows Pan Docs' "Using the STOP Instruction" diagram and SameBoy, with three simplifications, all marked
  as such in the code. Waking takes no time, where SameBoy spends 2 machine cycles. SameBoy's one-cycle hold of DIV in
  reset when IME is clear isn't modelled; SameBoy itself notes it as untrusted. A serial transfer on an external clock
  stands still while stopped, though the other Game Boy could still clock it. No licensed DMG game uses STOP.

## Graphics
- **Pixels within a line:** the LCD's timing is emulated to the machine cycle, but each line's pixels are drawn in one
  go as its mode 3 starts. Effects that change the scroll, palette, LCDC or window registers partway through mode 3
  aren't shown, and don't change how long that mode 3 lasts. Showing them would need a pixel FIFO.
- **Sprite size changes during mode 3:** sprites are selected and drawn as mode 3 starts, using LCDC bit 2 as it is
  then. What the hardware draws when the bit changes later in the line isn't clearly documented.
- **Interrupt edge timing:** several of gbmicrotest's ROMs in this area still fail. These are `oam_int_if_edge_b`
  and `_d` (the mode 2 interrupt flag rises one machine cycle later than on hardware), `lcdon_to_if_oam_b`,
  `line_144_oam_int_b`/`_c`/`_d`, `lcdon_to_oam_int_l0`/`_l1`/`_l2`, `oam_int_inc_sled` and `oam_int_nops_a`.
  SameBoy raises the mode 2 condition a clock before STAT changes, which may explain some of them.
- **STAT write bug:** `stat_write_glitch_l154_d` is left out, as it expects no V-Blank interrupt flag after line 144.
  That contradicts `stat_write_glitch_l143_d`, also checked on a DMG, as well as SameBoy and mGBA. SameBoy's special
  case at the H-Blank to mode 2 edge, where the write acts as ~$20 rather than $FF, isn't modelled.

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
- **Small details:** `App.cpp` checks the audio stream's status every frame, and the 1024-sample chunks add a little
  latency.

## Cartridges and saves
- **Unemulated mappers:** MMM01, MBC7, Pocket Camera, HuC1 and HuC3 cartridges run as MBC1. Their battery-backed RAM is
  still saved, but the HuC3's clock and the MBC7's EEPROM aren't.
- **2 KB RAM:** cartridges declaring 2 KB of RAM get 2 KB, as in BGB, SameBoy and VBA-M. mGBA gives them 8 KB, so
  homebrew that declares 2 KB by mistake but uses more works in mGBA and not here.

## Platforms
- **Windows:** the Visual Studio projects list every source file, but haven't been built or run since sound was added.
  That includes the bundled `openal32.dll` and the test project's older Google Test 1.7.
- **Linux:** CI builds and tests on Ubuntu, but the app hasn't been run on a Linux desktop.
- **Untried in the running app:** Ctrl+Cmd+F for fullscreen on macOS (which may clash with the system's own
  fullscreen shortcut), the default gamepad button numbers on each platform, and the save state keys F5-F8.
