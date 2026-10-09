# Nintendo 3DS build guide

The published stable release is [v1.0](https://github.com/EstebanPdN/legend-of-doom-3ds/releases/tag/v1.0). It targets New Nintendo 3DS, New Nintendo 3DS XL and New Nintendo 2DS XL, and uses `hardware-hybrid`. See the [main README](../../README.md) for installation and player controls.

## Requirements

- devkitPro/devkitARM, libctru and the devkitPro 3DS CMake toolchain.
- CMake, Python 3, Git, cURL, UnZip and a native C/C++ toolchain.
- SDL2 development files for the native host tools (`libsdl2-dev` in Linux CI).
- `makerom` and `bannertool` for CIA packaging. Pinned Linux x86_64 binaries are downloaded when needed. On macOS, supply native tools; `makerom-macos` and `bannertool-macos` are detected in the tools directory.

Dependency revisions and checksums are pinned in [dependencies.sh](dependencies.sh). The scripts fetch SDL2, ZMusic, minimp3, OpenAL Soft/NDSP, NovaGL, Freedoom and Legend of Doom, then apply the 3DS patches. Updater dependencies are described in [update-dependencies/README.md](update-dependencies/README.md).

## Build profiles

| Profile | Purpose |
|---|---|
| `hardware-hybrid` | Published v1.0 renderer: SoftPoly world rendering on CPU0/CPU2, OpenAL/NDSP audio and a bounded PICA200 texture presenter. |
| `hardware-safe` | Default build and GitHub Actions profile. CPU-only recovery renderer at 320×200 with SDL/libctru presentation and OpenAL/NDSP audio. |
| `release` | Legacy NovaGL world-renderer experiment. The name does not mean this is the published v1.0 profile. |
| `hardware-candidate` | NovaGL hardware investigation with audio, telemetry and automatic MAP01 startup. |
| `hardware-diagnostic` | The same diagnostic world-renderer path with audio disabled. |

Build the renderer profile used by the published release:

```sh
./platform/3ds/build.sh hardware-hybrid
```

Build the default recovery profile:

```sh
./platform/3ds/build.sh
```

A profile can also be selected through the environment:

```sh
LOD3DS_BUILD_PROFILE=hardware-hybrid ./platform/3ds/build.sh
```

The profile argument takes precedence over `LOD3DS_BUILD_PROFILE`. The version comes from [version.txt](version.txt), currently `1.0`.

## Configuration and outputs

| Variable | Purpose / default |
|---|---|
| `DEVKITPRO` | devkitPro root; `/opt/devkitpro`. |
| `DEVKITARM` | devkitARM root; `$DEVKITPRO/devkitARM`. |
| `LOD3DS_BUILD_ROOT` | Out-of-tree build directory; `build-3ds` at the repository root. |
| `LOD3DS_JOBS` | Parallel build jobs; `4`. |
| `LOD3DS_TOOLS_ROOT` | Directory with packaging tools; `../Tools/bin` relative to the repository. |
| `MAKEROM`, `BANNERTOOL` | Explicit packaging executable paths. |
| `LOD3DS_OPENAL_SOURCE_DIR` | Optional existing OpenAL Soft/NDSP checkout. |
| `LOD3DS_SKIP_CIA=1` | Build the 3DSX and SD ZIP without the CIA. |
| `LOD3DS_SCRIPT_VALIDATOR` | Optional native engine executable for the packaged game-script check. |

The distribution directory is `$LOD3DS_BUILD_ROOT/dist/`, or `build-3ds/dist/` by default. Local builds use a stem such as:

```text
legend-of-doom-3ds-v1.0-hardware-hybrid-<12-digit-build-id>
```

The generated files include `.3dsx`, `.cia` (unless skipped), `-sd.zip`, `-debug-symbols.zip`, `BUILD-MANIFEST.txt` and `SHA256SUMS.txt`. Public release assets use the shorter filenames linked from the main README.

The CIA embeds the matching engine and game data in RomFS. The 3DSX needs the matching SD ZIP's `3ds/legend-of-doom/data/` directory. Configuration, saves, logs and dumps use `sdmc:/3ds/legend-of-doom/` for both formats. The console's DSP firmware must be dumped separately to `sdmc:/3ds/dspfirm.cdc`.

## Renderer and runtime

The hybrid profile renders gameplay at 320×192 by default, with a configurable 50–100% render scale in 5% steps. Menus use native 400×240. CPU0 and CPU2 perform SoftPoly raster work; scene traversal stays single-owner and the audited desktop BSP worker is disabled on 3DS. OpenAL/NDSP handles audio on CPU1. PICA200 receives a completed frame texture rather than world geometry. NovaGL is linked but not initialized in this profile.

Rendering is uncapped and display-synchronized; Doom simulation remains at 35 Hz. A 60 Hz LCD does not establish a 60 FPS gameplay result. Record physical-console measurements separately from host and emulator results, and include the build ID, profile and display settings.

The CPU-only recovery profile uses a 320×200 canvas mapped to the top LCD through SDL/libctru linear framebuffers. It does not initialize NovaGL or Citro3D. Both CPU-renderer profiles start at the normal title menu, preserve SD saves/configuration and provide the diagnostic button chords below.

For the built-in CIA updater, its release format, TLS checks, host harness and console test procedure, see [UPDATER.md](UPDATER.md).

## Diagnostics and verification

| Button chord | Operation |
|---|---|
| `L + R + A` | Quick diagnostic capture: both screens, engine state, sanitized configuration, logs and build identity; no full RAM payload. |
| `L + R + X` | Full diagnostic capture including readable memory mappings. Use only when needed; it can exceed 130 MiB. |
| `L + R + Y` | Delete the diagnostic folders under `sdmc:/3ds/legend-of-doom/dumps/`. Saves and configuration are outside this cleanup. |

Attach the complete quick folder for ordinary bug reports. For updater failures, include `sdmc:/3ds/legend-of-doom/update.log`. Keep the matching manifest and debug symbols for address resolution; review full memory dumps before sharing them.

Useful local checks:

```sh
git diff --check
./platform/3ds/test-patches.sh
```

The build script also validates the patch files, banner structure and banner audio. GitHub Actions checks clean source, pinned patch application, package contents, public-file boundaries and output checksums, and builds `hardware-safe`. A successful CI build establishes package/build checks; gameplay and performance still require console evidence.

## Historical notes

The earlier version-by-version implementation notes are preserved in [history/README-before-v1.0.md](history/README-before-v1.0.md). The [August technical roadmap](../../docs/history/ROADMAP-2026-08-27.es.md), [v0.31 review](CODE-REVIEW-v031.es.md), [performance audit](PERFORMANCE-AUDIT.es.md) and versioned fix notes describe their original candidates and measurements. Use this guide and the published build manifest for the current release profile.
