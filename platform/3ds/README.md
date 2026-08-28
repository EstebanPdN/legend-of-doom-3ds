# Nintendo 3DS build

The supported hardware profile is New Nintendo 3DS/New Nintendo 2DS XL. The CIA embeds Freedoom, Legend of Doom and the matching GZDoom data in RomFS, so it is complete when installed by QR. The 3DSX keeps those resources in `sdmc:/3ds/legend-of-doom/data/`. World rendering currently uses the pinned NovaGL/Citro3D bridge at the top screen's native 400×240 resolution while the direct PICA200 backend is developed; the software renderer remains available only as a diagnostic fallback.

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
LOD3DS_BUILD_PROFILE  `release` (default), `hardware-candidate` or `hardware-diagnostic`; both hardware profiles keep L+R+A dumps/telemetry and open MAP01, while only `hardware-diagnostic` disables audio
```

The source and data revisions are pinned in `dependencies.sh`. Generated dependency checkouts live under `build-3ds/_deps/`; they are never committed. The ordered NovaGL patch stack is validated by `test-patches.sh`. The New 3DS profile uses one synchronized CPU/GPU frame slot, a 32 MiB linear arena, top-screen-only render targets, 256px texture clamping and CPU-writable linear storage for ordinary sampled textures; VRAM is reserved for render targets. Single buffering uses Citro3D's official `C3D_FRAME_SYNCDRAW` completion boundary before any ring or deferred allocation is reused. Merely counting asynchronously submitted frames is not a hardware fence. Clears are ordered through Citro3D's GX queue; the experimental shader-quad clear is disabled. Keeping sampled textures out of VRAM prevents the fixed 32-entry Citro3D GX queue from overflowing during startup uploads, while render-target creation fails cleanly instead of mislabelling linear RAM as renderable. NovaGL keeps a 3 MiB backing store for a complete frame, but cuts the stream at 192 KiB so no individual PICA submission approaches the 861,024-byte list that timed out on physical hardware. The 2 MiB vertex and 512 KiB index rings remain unchanged. The renderer clips walls, indexed floors and sprite strips at the perspective eye plane and all four homogeneous side planes, rejects batches wholly behind the camera without copying, reserves only the exact clipped stream, emulates GZDoom's portal clip plane, tracks `GL_DEPTH_CLAMP`, and prevents Early-Z from skipping stencil side effects. Native 20/24-byte wall and sprite VBOs use stable 32K-vertex bases with a persistent sequential-u16 quad table, avoiding per-draw buffer reprogramming while preserving the established triangle topology.

The diagnostic profile keeps the complete L+R+A artifact set (both screen captures, raw framebuffers, telemetry, engine state, logs and memory map). Per-draw GL validation and NovaGL trace logging are compiled out by default so the diagnostic build remains a meaningful performance measurement; explicit `NOVAGL_NO_DEBUG=OFF`, `NOVAGL_DRAW_DIAGNOSTICS=N` and `NOVAGL_TEXTURE_DIAGNOSTICS=N` overrides are available for a targeted deep renderer capture.

## Outputs

```text
build-3ds/dist/legend-of-doom-3ds-v0.1-<profile>-<build-id>.3dsx
build-3ds/dist/legend-of-doom-3ds-v0.1-<profile>-<build-id>.cia
build-3ds/dist/legend-of-doom-3ds-v0.1-<profile>-<build-id>-sd.zip
build-3ds/dist/SHA256SUMS.txt
build-3ds/dist/BUILD-MANIFEST.txt
build-3ds/dist/legend-of-doom-3ds-v0.1-<profile>-<build-id>-debug-symbols.zip
```

Every profile adds its name and 12-character source-state ID to every artifact. The debug ZIP contains the exact unstripped ELF, linker map, and manifest required to symbolize a Luma crash dump.

The `hardware-candidate` and `hardware-diagnostic` profiles enable a first-frame PICA200 supervisor.
The production profile keeps PICA early-Z disabled: the hardware supervisor
proved that its raw clear sequence could be submitted before Citro3D emitted a
framebuffer bind, wedging a 48-byte PICA command list on real New 3DS hardware.
The supervisor records bounded command words, framebuffer physical addresses,
buffer/attribute descriptors, VBO capacity and current linear/VRAM headroom for
the last draw issued by the CPU. Ordinary depth testing is unchanged. Draws are
submitted using Citro3D's normal frame queue. The renderer bounds natural PICA
segments at 192 KiB, while the diagnostic never waits, stops, clears or restarts
the GX queue between draws. At the real frame boundary it polls completion with
a two-second deadline. A timeout
writes `sdmc:/3ds/legend-of-doom/gpu-diagnostic.log` with the queue entries and
first incomplete GX operation, then uses `svcExitProcess` instead of entering
normal Citro3D teardown (which would wait forever on the wedged queue). A
successful first frame records `PASS` and disables the supervisor.

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
