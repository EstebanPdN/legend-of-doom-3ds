# Nintendo 3DS build

Current cleanup candidate: **v0.31**. See the [code review](CODE-REVIEW-v031.es.md)
for fixes, remaining bottlenecks and validation limits. Hybrid gameplay defaults
to 320×192, with 200×120 and 400×240 selectable; frame rendering is uncapped.
The version-by-version notes below are historical.

The supported hardware profile is New Nintendo 3DS/New Nintendo 2DS XL. The CIA embeds Freedoom, Legend of Doom and the matching GZDoom data in RomFS, so it is complete when installed by QR. The 3DSX keeps those resources in `sdmc:/3ds/legend-of-doom/data/`.

Version 0.13's `hardware-hybrid` profile is the performance build. It keeps
SoftPoly for world geometry, renders at 240×150, explicitly divides every
drawer command between CPU0 and New 3DS CPU2, leaves OpenAL/NDSP on CPU1, and
uses PICA200 only to upload and scale a single completed texture to 400×240.
NovaGL is not allowed to submit world geometry. Keep `hardware-safe` as the
recovery baseline. It renders at 320×200 on
the CPU and maps it across the complete 400×240 top LCD with a custom 0.96
pixel aspect (no side bars and no horizontal distortion), uses
SDL/libctru's CPU-writable linear RGBA8 scanout buffers, does not
initialize NovaGL/Citro3D, starts at the normal title/menu, and uses the pinned
OpenAL Soft/NDSP backend for audio. The stable profile uses the same GSP and DSP
service cache-maintenance paths as the physically working v0.6 package.
This deliberately trades speed for a path that does not submit PICA200
commands. `hardware-candidate` is retained only for developer investigation:
physical v0.11 evidence shows its third PICA segment can stall before the first
display transfer. The BSP worker is deliberately compiled out on 3DS after a
cross-core race audit; the desktop job queue and its roughly 600 KiB handheld
pool are not instantiated.

## Build

```sh
./platform/3ds/build.sh
```

Optional variables:

```text
DEVKITPRO             devkitPro root; defaults to /opt/devkitpro
LOD3DS_BUILD_ROOT     out-of-tree build directory
LOD3DS_JOBS           parallel build jobs; defaults to 4
LOD3DS_TOOLS_ROOT     directory containing makerom and bannertool
MAKEROM               explicit makerom executable
BANNERTOOL            explicit bannertool executable
LOD3DS_OPENAL_SOURCE_DIR  optional existing OpenAL Soft/NDSP checkout
LOD3DS_SKIP_CIA=1     build only 3DSX and SD ZIP
LOD3DS_BUILD_PROFILE  `hardware-safe` (default), `hardware-hybrid`, `release`, `hardware-candidate` or `hardware-diagnostic`; `hardware-hybrid` is the CPU0+CPU2 SoftPoly/PICA presenter path, `hardware-safe` is the CPU-only recovery path, `release` is legacy NovaGL, and the diagnostic GPU profiles open MAP01
```

Recommended performance build:

```sh
./platform/3ds/build.sh hardware-hybrid
```

If the hybrid path cannot reach gameplay, install the separately packaged
`hardware-safe` build to retain the known-good CPU-rendered recovery path and
collect a quick dump.

An embedded 96×96, 33-frame Triforce animation appears as soon as SDL owns the
RGBA8 scanout and remains centered while GZDoom parses its data. The original
500×500 GIF is converted offline to a roughly 67 KiB RLE stream, so startup
does not need a GIF decoder, filesystem access or large frame allocations.
The first game/menu frame can still take several seconds.
If HOME or close is requested while the main thread is stuck for more than 8
seconds, an independent New 3DS supervisor terminates only this process rather
than entering a GPU teardown wait that would require a forced power-off.

The source and data revisions are pinned in `dependencies.sh`. Generated dependency checkouts live under `build-3ds/_deps/`; they are never committed. The ordered NovaGL and OpenAL patch stacks are validated by `test-patches.sh`. The `hardware-safe` 3DSX assigns 92 MiB to the conventional heap and 4 MiB to linear memory. The CIA requests 124 MiB through the New 3DS field and uses the retail-compatible 64 MiB legacy fallback. Its startup allocator measures the resource limit actually granted, caps the conventional heap at 92 MiB (a 4 MiB guard below libctru's documented virtual-range edge), and assigns the bounded remainder to linear memory. Both allocations retry downward instead of panicking on optional headroom. This avoids libctru's rejected 109.328 MiB fixed-address request seen in physical dumps 00000107 and 00000109 while remaining safe if a launcher supplies only the legacy arena. SoftPoly holds a 2 MiB contiguous block during script parsing, releases it immediately before creating its reduced 65,536-vertex (1.25 MiB) buffer, and therefore does not depend on aggregate-but-fragmented heap space. SDL's upstream N3DS framebuffer stays in linear RAM because its software presenter writes scanout pixels with the CPU. Audio is provided only by the pinned OpenAL Soft/NDSP backend; SDL audio remains compiled out so two drivers cannot compete for DSP ownership. Its mixer keeps eight PCM buffers queued and uses `svcStoreProcessDataCache` for the CPU-to-DSP clean, retaining `DSP_FlushDataCache` as a compatibility fallback if the launcher rejects the direct SVC. The New 3DS OpenAL listener keeps neutral 1x output gain so the user's master/music/effects sliders remain authoritative and the PCM16 limiter is not forced into boosted operation. The NDSP lifetime is tied to the OpenAL backend, so device destruction joins libctru's worker before process teardown can release its stack. Quick and full dumps pause the complete OpenAL device, then restore the prior music state and active output on every exit path.

The GPU profiles use one CPU/GPU frame slot, a 32 MiB linear arena, top-screen-only render targets, 256px texture clamping and CPU-writable linear storage for ordinary sampled textures; VRAM is reserved for render targets. Before a ring, deferred allocation or GPU-written range can be reused by the CPU, NovaGL drains the pinned Citro3D render queue with `C3Di_RenderQueueWaitDone`; `C3D_FRAME_SYNCDRAW` is only display-counter synchronization and is not treated as a GPU fence. Every CPU-written texture, VBO, immediate/index ring and one-shot transfer staging range is flushed explicitly. Every intermediate command-list split carries `GX_CMDLIST_FLUSH`; frame retirement currently keeps Citro3D's conservative `C3D_FrameEnd(0)` whole-linear-heap fallback because the v0.8 physical dump disproved the range-only recovery. The three Citro3D sync helpers are wrapped because they perform hidden splits. GPU-to-CPU paths invalidate around the completed transfer, and deferred frees occur only after the queue fence. `C3Di_RenderQueueWaitDone` is private API, so the pinned Citro3D revision is part of this contract and must be re-audited on any toolchain upgrade. Clears remain ordered through Citro3D's GX queue; the experimental shader-quad clear is disabled. Keeping sampled textures out of VRAM prevents the fixed 32-entry Citro3D GX queue from overflowing during startup uploads, while render-target creation fails cleanly instead of mislabelling linear RAM as renderable. NovaGL keeps a 3 MiB backing store for a complete frame and cuts every stream, including the supervised first frame, at 192 KiB. This caps automatic PICA submissions at 16; the rejected 64 KiB diagnostic policy could create 48 entries and overflow Citro3D's fixed 32-entry GX queue. The 2 MiB vertex and 512 KiB index rings remain unchanged. The renderer caches the combined model-view-projection matrix once per dirty transform, clips walls, indexed floors and sprite strips at the perspective eye plane and all four homogeneous side planes, rejects batches wholly behind the camera without copying, reserves only the exact clipped stream, emulates GZDoom's portal clip plane, tracks `GL_DEPTH_CLAMP`, and prevents Early-Z from skipping stencil side effects. Native 20/24-byte wall and sprite VBOs use stable 32K-vertex bases with a persistent sequential-u16 quad table, avoiding per-draw buffer reprogramming while preserving the established triangle topology.

Every profile provides three diagnostic chords and matching touch cards. `L+R+A` creates the default quick dump: both screens, raw framebuffers, engine state, sanitized configuration, logs/telemetry and a small memory-map survey, but no RAM payload. `L+R+X` creates a full dump with the same files plus one `memory.bin` containing the concatenated readable mappings (roughly 136 MiB on the current New 3DS layout) and offsets in `memory-map.txt`. Version 0.3 keeps the payload complete but replaces 25 separately flushed files and a bytewise FNV pass with one atomic stream, 512 KiB chunks and no on-console payload hash. Elapsed time and throughput are recorded in the manifest. `L+R+Y` recursively deletes only the contents of `sdmc:/3ds/legend-of-doom/dumps`, leaving configuration, saves and ordinary logs untouched. Quick and full requests wait one second, copy both ordinary LCD views into about 691 KiB of immutable RAM, immediately swap in their full-screen progress display, and only then perform slow FAT writes and BMP conversion; clean uses the same progress display immediately. Before writing a payload, the writer measures its bounded artifacts and, for a full dump, all readable mappings; it refuses the operation with `YOUR MEMORY IS FULL` unless the payload and an 8 MiB safety reserve fit. The bottom panel has solid-black surfaces, white text and exact `#163eff` borders. Temporary messages replace the contents inside the existing status card and restore its normal text when they expire. Per-draw GL validation and NovaGL trace logging are compiled out by default; explicit `NOVAGL_NO_DEBUG=OFF`, `NOVAGL_DRAW_DIAGNOSTICS=N` and `NOVAGL_TEXTURE_DIAGNOSTICS=N` overrides remain available for targeted GPU investigation.

Version 0.4 packages native 400x240 replacements for the mod's square `TITLEPIC` and idle-sequence `ZSTORY` credit page, plus a native 320x240 bottom-menu illustration generated from the supplied artwork. The loading Triforce remains the first visible screen. Once GZDoom reaches its title/menu state, the custom art occupies both LCDs; the performance/dump overlay and its touch hitboxes activate only while `GS_LEVEL` is running. Full-screen diagnostic progress still takes precedence whenever a dump or cleanup operation is active.

Version 0.5 refreshes the bottom illustration, explicitly follows the title sequencer's current page, and matches the two LCDs during interaction: `ZSTORY` makes the lower LCD black, returning to `TITLEPIC` restores the illustration, and opening Start/New Game applies the mod's 95% black menu dim to the lower image too. Presentation-state changes bypass the overlay's 250 ms statistics throttle so the two screens switch together. The lore source uses a reducing prepass plus bilinear minification instead of Lanczos, avoiding high-contrast ringing around its pixel-font strokes.

Version 0.6 replaces all three supplied menu assets again. Its `TITLEPIC` and `ZSTORY` sources are already native 400x240, so the build preserves their pixels without any resize filter; only the larger lower-screen illustration is converted to 320x240 and RGB565 for scanout.

Version 0.8 is the physical-hardware recovery revision of the 0.7 performance pass. It keeps explicit cache-range ownership, safe transfer staging, deferred resource destruction and cached MVP composition, but restores Citro3D's conservative full-linear cache flush at frame retirement after New 3DS dumps proved that the range-only contract could wedge the first MAP01 PICA command list. The first-frame queue wait is also bounded in diagnostic builds, so a future GPU fault writes a complete report and exits instead of leaving the Triforce animation on screen indefinitely. Texture sorting remains enabled while actor shadows, dynamic lights, mip/filter work, expensive particle styling and deep portal recursion are disabled for the handheld budget. Rendering is capped at 30 FPS without changing Doom's 35 Hz game simulation; interpolation remains enabled. A stable 30 FPS claim still requires a fresh 600-frame capture on physical New 3DS hardware; emulator timing is not accepted as performance evidence.

Version 0.9 corrects the failed v0.8 recovery assumption. The v0.8 dump shows that the sixth GX entry—the third large PICA list—stopped making progress even though the full linear-heap flush was already active. The new build therefore validates the command inputs themselves: IEEE-754 finiteness checks remain effective under NovaGL's `-ffast-math`, every indexed/VBO path proves its complete range, transformed clip coordinates are checked, homogeneous side-plane crossings are clipped, invalid matrices and empty viewport/scissor regions are discarded, and the LCD transfer format now matches SDL's RGBA8 scanout. Unsafe clipping allocation failures drop that batch instead of falling back to the original geometry. These are source-level safety corrections, not proof that the hardware fault is gone; only a physical New 3DS boot can establish that.

Version 0.10 adds an opt-in hardware draw-range probe used only to localize a
stalled PICA segment. Its default stride is zero, so ordinary candidates do not
split or wait every N draws.

Version 0.11 applies the directly transferable, hardware-measured techniques
from [zelda-tmc-3ds PR 26](https://github.com/EstebanPdN/zelda-tmc-3ds/pull/26).
Every existing NovaGL CPU-to-GPU cache-clean site now routes through
`svcStoreProcessDataCache`, with the synchronous GSP service retained only as a
launcher fallback. OpenAL uses the same clean-only semantic for PCM, and fixes
the app share of core 1 to 30% for the lifetime of its worker, restoring the
previous value on shutdown. This leaves 70% of core 1 to system modules such as
GSP, which is responsible for retiring the GX queue that stalled in the v0.9
physical dump. Static indices, bounded dirty ranges, state deduplication,
buffered telemetry, skipped redundant bottom redraws and a retained upload
representation were already present, so they were audited rather than
duplicated. The candidate is not called functional or 30 FPS until a physical
New 3DS proves both.

Version 0.12 resolves the boot regression by making the proven CPU renderer the
only distributed path. The v0.11 console dump reaches MAP01 and starts music,
then records five seconds without progress in PICA queue entry 5 (draws
631–1216); the following LCD transfer never runs, which is why the completed
Triforce remains visible. All v0.7–v0.11 QR codes selected a GPU candidate.
The v0.12 stable build forces SoftPoly regardless of an archived INI, restores
the v0.6 SDL presenter and DSP cache flush, rejects NovaGL init/swap entry
points and the experimental direct cache SVC during packaging, and records the
first three successful CPU presents.

Version 0.13 attacks the measured CPU bottleneck without restoring the failed
NovaGL world path. Physical v0.12 dumps measured 12.3 FPS while facing a wall
and 6.2–6.6 FPS in the normal scene, with ample conventional and linear memory;
the cost therefore follows visible pixels and scene work rather than OOM. The
hybrid profile reduces raster pixels by 43.75%, uses explicit libctru workers on
CPU0/core2, keeps scene traversal single-owner to avoid the audited BSP race,
and replaces SDL's CPU scale/rotation with one bounded PICA200 texture quad.
It retains the v0.12 allocator, OpenAL/NDSP, startup animation and clean SDL
fallback. A stable 15 or 30 FPS result remains a physical-console measurement,
not a build-time guarantee.

Version 0.15 replaced the classic SKYWW texture/visplane with a constant BGRA
blue background and removed the 30 FPS software cap. Physical dumps later
proved that MAP01's separate SkyViewpoint portal still performed a second BSP
render and drew its cloud/geometry over that background.

Version 0.16 removes that remaining cost at its source: MAP01 skips only
`PORTS_SKYVIEWPOINT` plane portals, while stacked and linked gameplay portals
remain enabled. Dumps count both flat-background fills and skipped sky portal
planes. Gameplay stays at 240x150 in the hybrid profile, but menus and the
console switch dynamically to the LCD's native 400x240 canvas. Death adds a
subtle persistent red tint. Diagnostic dumps pause the complete OpenAL device,
and the forced +6 dB listener boost is removed so the PCM limiter is not driven
unnecessarily. Rendering remains uncapped and synchronized to the 60 Hz LCD.

Version 0.17 corrects the flat-sky diagnosis using the directional dump set.
MAP01's SKYWW cloud texture is restored, while the remote SkyViewpoint geometry
remains disabled. In outdoor sectors, a 1536-unit root-BSP/line/sprite limit
prevents traversal into the dense far half of the original isometric map; the
software colormap fades geometry toward a pale horizon before the cutoff.
Hybrid gameplay defaults to 320x192 and PICA200 scales it bilinearly. The lower
LCD cycles 10%-100% resolution and toggles a 30 FPS cap; both default to 80%
and uncapped. Title UI remains native, while in-level menus are a stable layer
over the gameplay canvas so New Game no longer shows one enlarged world frame.

Version 0.18 is based on the paired 001/002/003 physical dumps, which show that
the v0.17 pop and floor holes were not independent texture defects. The first
distance implementation rejected a complete BSP subtree before its subsectors
could contribute both wall segments and floor/water visplanes. It also relied
on Doom's ordinary light falloff instead of defining where the fade begins and
ends, and its below-horizon fallback used a different colour from the terminal
fog. Those three decisions made a wall, its adjacent floor and water disappear
together into a conspicuous rectangle.

The outdoor cutoff is now 2048 map units, 33.3% farther than v0.17. A dedicated
smoothstep visibility curve starts at 1536 and reaches the shared horizon colour
at the cutoff for walls, horizontal planes and sprites. The SKYWW clouds use the
separate sky path and are never fogged. Whole-subtree rejection is legal only
when every descendant subsector is exterior; a subtree containing an interior
room or black doorway is preserved. The 001-005 follow-up dumps proved that
this classification was still insufficient: MAP01 also gives `F_SKY1` ceilings
to several black/interior sectors, and a remote subtree can own the visplane or
occluder of near screen columns. Version 0.18 therefore traverses the complete
BSP topology and applies the cutoff at its safe leaves. Distant lines still use
the cheap far-clip path, which contributes clipping and visplane bounds without
drawing the textured wall; distant sprites are rejected before projection.
The terminal uncovered-pixel fill uses the same colour as the geometry fog.
Gameplay and its HUD default to 320x192 (80%), with touch-selectable 200x120,
320x192 and 400x240 modes and bilinear PICA200
presentation; menus and the console use 400x240. Cross-resolution wipe captures
resample the complete 400x240 menu into the 320x192 transition surface instead
of copying a 320x192 top-left crop. The renderer is always uncapped, the lower
LCD again shows the quick/full/clean dump cards, and new packages are named
`001-full-YYYYMMDD-HHMMSS`, then `002-...`, with the sequence recovered from
existing dump directories after a restart.

Version 0.19 uses the 001/002 v0.18 dumps to close the two remaining physical
defects. MAP01's cave sectors deliberately combine an `F_SKY1` ceiling with a
`BLACK` floor; treating every sky-ceiling sector as outdoors faded that black
entrance to the exact blue terminal fog colour. Black-floor sectors now remain
ordinary opaque interiors while surrounding outdoor geometry keeps the same
distance fade. The music dump captured a healthy MP3 decoder at 44.58 seconds
and an OpenAL source still in `AL_PLAYING`, localizing the silence after the
decoder in the OpenAL-to-NDSP handoff. The NDSP queue grows from four to eight
wave buffers and its CPU-to-DSP cache clean avoids the synchronous service
round trip; the old DSP service remains a launcher-compatibility fallback.

Version 0.24 returns to the v0.22 interface base and routes the mod's existing
engine-rendered `ListMenu` and `OptionMenu` pixels to the lower LCD. BigFont,
the Link selector, selection state, sliders and dialogs therefore all come from
the original menu implementation; inverse touch mapping feeds that same menu's
responder. The pause view preserves the dimmed live game on top with a centered
logo. Display exposes 50%, 80% and 100% gameplay rendering, a functional top-HUD
toggle and an opt-in crosshair. Developer exposes FPS, quick/full dumps and an
optional Select-triggered lower-screen overlay. The gameplay panel adds a
contained 48x58 animated face, padded hearts and centered item counters. Tapping
the recentered map toggles between its normal and wide zoom. The lore page keeps
the lower LCD completely black.

Version 0.25 polishes that routed interface from hardware dumps 003-013. Menus
are enlarged without replacing the mod UI, option titles receive a stronger
scale, pause menus use a black lower background, and the top logo is larger and
visually centered. Volume exposes independent master, music and effects
sliders. The gameplay panel has adaptive hearts, a smaller counter cluster, a
rebalanced face and tabs, a lower map with optional collision lines, a working
aim cross, the Minish Cap-style FPS plate, and the restored technical Select
overlay. Confirmation choices are horizontal and touch-enabled.

Version 0.26 follows the hardware dumps 001-018. It restores the exact compact
v0.14 diagnostics layout for Select, moves the FPS and frame-time plate to the
upper-right LCD, drives the real 50/80/100 percent render canvas, and builds the
small centered crosshair from the supplied PNG. Known menus retain their real
engine actions and Link selector while crisp integer pixel labels remove the
broken fractional BigFont downscale and keep every selection stationary. NPC
ACS dialogue remains visible with Top HUD disabled, pickup notices gain a black
shadow, and the lower automap resets by map name so dungeon maps center correctly.
The item selector is a rounded square; hearts, counters, shield and tab-frame
spacing are refined from the new dumps.

Version 0.27 follows quick dumps 001-007 from the v0.26 hardware pass. All
menus use the same engine SmallFont and layout as the approved Master Volume
screen, the extra upper-screen pause logo is gone, and Controller adds C-Stick
sensitivity, C-Stick/Touch/Both look selection, optional full-health X sprint
and a read-only controls page. The fixed handheld layout uses R/L for
primary/alternate attack, D-pad left/right for weapons and ZL/ZR for inventory.
FPS and the supplied crosshair are composited directly into the final canvas at
all three render scales. D-pad up replaces the brown automap with an enlarged
version of the explored floor-textured map, the weapon pair moves ten pixels
left, counters grow another 15 percent, and the opening cave message is centered
as two explicit lines.

Version 0.28 follows quick dumps 001-006 from the v0.27 hardware pass. Option
labels now share one fixed right-aligned column, while the read-only controls
reference uses separate Input and Action columns. FPS typography scales down at
50 and 80 percent, D-pad Up opens a distinct full-screen low-zoom terrain
overview, the smaller crosshair sits ten physical pixels lower, and the weapon
pair moves five pixels right. Inventory cells are directly selectable by touch.
The v0.25 two-screen menu composition returns with the current SmallFont and
stationary selection geometry. Save and Load place their title, enlarged
screenshot and metadata on the upper LCD, leave only an enlarged touchable slot
list below, and New Save Game uses the native 3DS keyboard.

The physical-only sky corruption was in the software scene path, not in the WAD texture, aspect conversion or screenshot writer. Clearing all four BGRA bytes per canvas pixel removed the old-frame ghosting. The newer raw dumps then exposed the remaining defect precisely: valid sky pixels were opaque, while the black wedges retained the untouched alpha-zero clear value between disjoint classic sky-visplane spans. Version 0.2 recorded zero fallback calls because MAP01 never reached its visplane-triggered guard. Version 0.3 schedules the level's primary sky explicitly once per main view, immediately after ordinary planes and before portal passes, and projects it through the normal column sampler only into still-transparent runs. Opaque walls, flats, deliberately black textures, transferred skies, portal windows, auxiliary canvases and the HUD are never replaced. Each dump records the fallback call count and filled-pixel count for physical verification. The ARM path also tests only negative clip half-spaces and bounds-checks span arrays before reading them.

## Outputs

```text
build-3ds/dist/legend-of-doom-3ds-v<version>-<profile>-<build-id>.3dsx
build-3ds/dist/legend-of-doom-3ds-v<version>-<profile>-<build-id>.cia
build-3ds/dist/legend-of-doom-3ds-v<version>-<profile>-<build-id>-sd.zip
build-3ds/dist/SHA256SUMS.txt
build-3ds/dist/BUILD-MANIFEST.txt
build-3ds/dist/legend-of-doom-3ds-v<version>-<profile>-<build-id>-debug-symbols.zip
```

Every profile adds its name and 12-character source-state ID to every artifact. The debug ZIP contains the exact unstripped ELF, linker map, and manifest required to symbolize a Luma crash dump.

The `hardware-candidate` and `hardware-diagnostic` profiles enable a first-frame PICA200 supervisor.
The production profile keeps PICA early-Z disabled: the hardware supervisor
proved that its raw clear sequence could be submitted before Citro3D emitted a
framebuffer bind, wedging a 48-byte PICA command list on real New 3DS hardware.
The supervisor records every first-frame PICA draw in a fixed RAM table and
correlates each command-list virtual address with its draw range, framebuffer,
VBO/EBO, texture and raster state. Ordinary depth testing is unchanged. Draws
are submitted using Citro3D's normal frame queue; the diagnostic never waits,
stops, clears or restarts that queue between draws. Every frame uses bounded
192 KiB segments, including the supervised first frame, so the diagnostic
cannot exhaust Citro3D's 32-entry GX queue. At the real frame
boundary it resets a five-second deadline whenever the GX completion index
advances, so a slow frame is not confused with a stalled command. A no-progress
timeout writes `sdmc:/3ds/legend-of-doom/gpu-diagnostic.log` plus the exact
`gpu-command-segment.bin`, then uses `svcExitProcess` instead of entering normal
Citro3D teardown. A successful first frame records `PASS` and disables the
supervisor. `NOVAGL_DRAW_CUTOFF=N` is a real first-frame PICA prefix switch for
hardware-only binary search if a later dump still requires isolation.

Both hardware profiles also write a bounded
`sdmc:/3ds/legend-of-doom/frame-telemetry.csv` segment. It records render/present
wall time, Citro3D command-processing and GPU drawing time, plus heap, linear
memory, VRAM, draw calls, submitted vertices and topology counts once per
frame. Rows accumulate in RAM and are written as one 720-frame segment (or
immediately before an L+R+A dump), so SD open/flush latency is outside ordinary
measured frames. These aggregate counters measure batching opportunities without
per-draw logging. Summarize a 120-frame warm-up followed by 600
frames on the host with:

```sh
python3 platform/3ds/tools/summarize-telemetry.py frame-telemetry.csv
```

Use `--json` for CI or plotting input. Patch-stack drift can be checked without
building the game:

```sh
./platform/3ds/test-patches.sh
```

The SD ZIP contains the exact directory layout needed by the 3DSX build and writable CIA state such as configs, saves, logs and diagnostics. The CIA does not read engine/mod data from SD.

## Release validation

A package build is not sufficient for release. Before publishing, the exact candidate must boot, render, accept controls, save, and remain running on physical New Nintendo 3DS hardware. Emulator runs are outside the validation plan because they do not reproduce the current hardware-only GPU failure. Frame-rate claims require sustained real-hardware measurements.
