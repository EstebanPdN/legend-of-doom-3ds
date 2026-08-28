Legend of Doom 3DS v0.1
========================

Copy the included "3ds" folder to the root of your Nintendo 3DS SD card.

For the Homebrew Launcher, start:
  sdmc:/3ds/legend-of-doom/legend-of-doom-3ds.3dsx

For the CIA build, install the CIA with FBI. Freedoom, Legend of Doom and the
matching GZDoom resources are embedded in the CIA; the data folder is not
required. Configs, saves, logs and diagnostic dumps are still written under:
  sdmc:/3ds/legend-of-doom/

Audio on a real console requires that console's own DSP firmware at:
  sdmc:/3ds/dspfirm.cdc
With Luma3DS, open Rosalina, select "Miscellaneous options...", then
"Dump DSP firmware". This system file is not distributed with the port.

The data folder contains Freedoom: Phase 2, the Legend of Doom mod, and the
GZDoom runtime resources. No NES or commercial Doom ROM/WAD is required.

If BUILD-MANIFEST.txt says "profile=hardware-candidate", audio remains enabled,
MAP01 opens automatically and the port exercises both 2D and world rendering.
The "hardware-diagnostic" profile runs the same path with audio disabled.
Its engine log is written to:
  sdmc:/3ds/legend-of-doom/boot.log
The first bounded NovaGL draw trace is written to:
  sdmc:/3ds/legend-of-doom/novagl.log
Immediately-flushed startup checkpoints are written independently to:
  sdmc:/3ds/legend-of-doom/startup.log

Press L + R + A together once to request a diagnostic package in every build
profile. It captures both screens (raw and BMP), engine/map/player/input/render
state, build identity, logs/telemetry and all readable process-memory mappings.
Keep the exact BUILD-MANIFEST.txt with any Luma crash dump so its addresses can
be matched to the correct ELF and linker map.

Legend of Doom was created by DeTwelve Games. See CREDITS.md and
THIRD-PARTY-LICENSES.md; exact component terms are preserved under licenses/.
