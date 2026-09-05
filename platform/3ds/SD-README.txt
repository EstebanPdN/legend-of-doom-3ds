Legend of Doom 3DS v0.28
========================

Copy the included "3ds" folder to the root of your Nintendo 3DS SD card.

For the Homebrew Launcher, start:
  sdmc:/3ds/legend-of-doom/legend-of-doom-3ds.3dsx

For the CIA build, install the CIA with FBI. Freedoom, Legend of Doom and the
matching GZDoom resources are embedded in the CIA; the data folder is not
required. Configs, saves, logs and diagnostic dumps are still written under:
  sdmc:/3ds/legend-of-doom/

A small animated Triforce appears immediately while the engine data loads and
disappears automatically when the normal menu renderer is ready.
The title menu uses supplied pixel-exact 400x240 artwork on the top LCD. The
original Legend of Doom menu actions and Link selector are routed to the lower
LCD with a crisp integer pixel font and direct touch rows; the pause menu uses
the same actions instead of a second disconnected interface.
When the idle title sequence advances to the Zelda lore page, that page also
uses its supplied native 400x240 replacement instead of the old square art.
The lower LCD turns completely black for that lore page and restores its
illustration with the title. In game, opening the menu keeps the live scene
visible through the normal translucent dim while the centered Doom logo stays
on the top LCD. The lore text is supplied at native 400x240 and is packaged
without resampling.

Audio on a real console requires that console's own DSP firmware at:
  sdmc:/3ds/dspfirm.cdc
With Luma3DS, open Rosalina, select "Miscellaneous options...", then
"Dump DSP firmware". This system file is not distributed with the port.

If BUILD-MANIFEST.txt says "profile=hardware-hybrid", this v0.28 package keeps
the proven SoftPoly world renderer and defaults its gameplay canvas to 320x192
and splits every drawer command between explicit libctru workers on CPU0 and
New 3DS CPU2. OpenAL/NDSP owns CPU1. PICA200 receives only one bounded texture
upload and one Citro2D quad per completed frame to scale it to 400x240; NovaGL
never receives world geometry. If GPU presenter initialization fails cleanly,
the same frame can still fall back to SDL. PICA200 applies bilinear scaling.
Menus and the console use the native 400x240 canvas. Gameplay defaults to 80%
and can be changed from Display to 50%, 80% or 100%. Display can also disable
the top HUD or enable the supplied small aiming crosshair. The render selector
changes the real canvas immediately and survives restarts. MAP01 restores the SKYWW cloud
texture, skips only the remote
SkyViewpoint geometry, and fades outdoor walls, floors, water and sprites with
a smooth curve from 1536 through 2048 map units. The complete BSP topology is
traversed so every near sky, floor, room and black doorway keeps its occlusion;
only distant line texture work and sprite projection are discarded. Menu-to-
game wipes resample the whole native menu frame into the gameplay surface, so
the transition cannot expose a zoomed top-left crop. Sky and clouds remain
separate from the geometry fog. Cave sectors whose BLACK floor represents
solid darkness are excluded from the outdoor blue fog. Rendering is always uncapped; Doom simulation remains 35 Hz
and presentation may reach the 60 Hz LCD rate. Exact FPS must be measured on
the physical New 3DS.

If BUILD-MANIFEST.txt says "profile=hardware-safe", this recovery package uses the
320x200 CPU renderer mapped to the full 400x240 top LCD with a custom 0.96
pixel aspect (no side bars or horizontal distortion), SDL/libctru
linear framebuffers and OpenAL/NDSP audio. The OpenAL listener uses neutral
1x gain; master, music and effects sliders remain user controlled without
forcing the PCM limiter into a boosted path.
Rendered PCM uses an eight-buffer NDSP queue and a low-jitter process-cache
clean, with the DSP service retained as a compatibility fallback. The bottom
screen identifies this package as V0.28 during
gameplay. Diagnostic dumps pause the entire audio device and their manifests
record the version, selected gameplay resolution, distance-cull counters and
skipped-SkyViewpoint counters.
It does not enter MAP01 automatically and never initializes NovaGL or
submits a PICA200 command list. Wait through the initial data parse until the
normal title/menu appears. HOME/close has an 8-second emergency supervisor so
a wedged main thread exits instead of requiring a forced console power-off.

The data folder contains Freedoom: Phase 2, the Legend of Doom mod, and the
GZDoom runtime resources. No NES or commercial Doom ROM/WAD is required.

If BUILD-MANIFEST.txt says "profile=hardware-candidate", audio remains enabled,
MAP01 opens automatically and the port exercises both 2D and world rendering.
The "hardware-diagnostic" profile runs the same path with audio disabled.
These PICA profiles use deterministic same-thread BSP preparation, texture/state
sorting, explicit CPU/GPU cache ranges and a real Citro3D render-queue fence
before reusing GPU-visible storage. The unsafe desktop-style BSP job pool is
compiled out on 3DS. The renderer is uncapped while Doom simulation continues
at 35 Hz with interpolation. Dynamic lights, actor shadows, costly
particle styling and deep portal recursion are disabled for the handheld
budget. Performance is a physical-console measurement, not a claim: validate
this exact build on a New 3DS and do not use emulator timings as evidence.
The v0.11 physical dump proved that its third PICA command-list segment stopped
before the first display transfer despite the v0.9-v0.11 validation, cache and
queue changes. These GPU profiles remain developer-only experiments. The v0.12
hardware-safe package never enters them, even if an old configuration requests
the hardware renderer.
Its engine log is written to:
  sdmc:/3ds/legend-of-doom/boot.log
The first bounded NovaGL draw trace is written to:
  sdmc:/3ds/legend-of-doom/novagl.log
Immediately-flushed startup checkpoints are written independently to:
  sdmc:/3ds/legend-of-doom/startup.log

Press L + R + A together once for the default quick diagnostic package. It
captures both screens (raw and BMP), engine/map/player/input/render state,
build identity, sanitized config, logs/telemetry and a memory-map survey, but
does not copy the large process-memory payload. Press L + R + X only when a
full dump is needed; it adds all readable memory mappings and can exceed 130 MB.
The payload is one memory.bin plus memory-map.txt. This build writes it with
one open/close cycle, 512 KiB chunks and no slow on-console bytewise hash.
Press L + R + Y to delete every package under the dumps directory. The three
cards are also touch buttons. Quick and full wait one second, copy both
ordinary LCD images to RAM, and then show the full-screen progress bar before
the slow SD/BMP work begins; clean starts its progress bar immediately. Music
pauses during quick/full writes and resumes in its previous state. Before
writing, the game measures
the required payload and preserves 8 MiB of free space; if it will not fit it
shows "YOUR MEMORY IS FULL" without beginning a partial dump. The panel uses a
black background, white text and #163eff borders. Temporary notifications
replace the text inside the status card and restore it when they expire.
Dump directory names put a persistent three-digit sequence first, beginning at
001, followed by the mode and timestamp, for example:
  001-full-20260831-190000
The next package is 002 even after restarting, provided the earlier directory
remains under sdmc:/3ds/legend-of-doom/dumps/.
The NDSP worker is now joined when the OpenAL device is destroyed, before its
thread stack can be released during process exit. Keep the exact
BUILD-MANIFEST.txt with any Luma crash dump so its addresses can
be matched to the correct ELF and linker map.

Legend of Doom was created by DeTwelve Games. See CREDITS.md and
THIRD-PARTY-LICENSES.md; exact component terms are preserved under licenses/.
