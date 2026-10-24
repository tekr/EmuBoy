# Bundled OpenAL runtime

`x86/openal32.dll` and `x64/openal32.dll` are the OpenAL Soft runtime that
`sfml-audio-2.dll` imports. The SFML 2.4.0 NuGet redist packages listed in
`../packages.config` do not include it, so `UI.vcxproj` copies the matching
DLL next to `EmuBoy.exe` after each build (`$(Platform)` Win32 -> `x86`, x64 -> `x64`).
Without it, EmuBoy.exe fails to start on machines that do not have OpenAL installed.

## Source

Taken unmodified from `bin/openal32.dll` in the official SFML 2.4.0 SDKs for Visual C++ 14 (2015):

- 32-bit: https://www.sfml-dev.org/files/SFML-2.4.0-windows-vc14-32-bit.zip
  (PE32, Intel 80386) SHA-256 `866e65bcdba3161c5f1d0300d4ae1a0b663511ba88993d442c5f93f570258d11`
- 64-bit: https://www.sfml-dev.org/files/SFML-2.4.0-windows-vc14-64-bit.zip
  (PE32+, x86-64) SHA-256 `9719913665c1ec5a4adc1d1377b695332599d627dc3ccde3d9cdea1855902bb5`

Download page: https://www.sfml-dev.org/download/sfml/2.4.0/

## Licence

OpenAL Soft is licensed under the GNU Lesser General Public License (LGPL).
The SFML SDK's `license.txt` states, under "External libraries used by SFML":

> * OpenAL-Soft is under the LGPL license

Project home and source code: https://openal-soft.org/

The SFML SDK does not ship the LGPL text itself; the full licence is at
https://www.gnu.org/licenses/lgpl-2.1.html (OpenAL Soft's own licence terms are in its source distribution).
Because the DLL is a separate, unmodified shared library loaded at run time, it can be replaced
by any compatible OpenAL Soft build.
