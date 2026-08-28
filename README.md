# Legend of Doom 3DS

![Legend of Doom 3DS](platform/3ds/assets/splash.png)

[![Build Nintendo 3DS packages](https://github.com/EstebanPdN/legend-of-doom-3ds/actions/workflows/build-3ds.yml/badge.svg)](https://github.com/EstebanPdN/legend-of-doom-3ds/actions/workflows/build-3ds.yml)

Nintendo 3DS port of [Legend of Doom](https://github.com/emawind84/legend-of-doom), the first-person GZDoom conversion of the original *The Legend of Zelda* created by DeTwelve Games.

The port is based on GZDoom 4.7.1 and uses the freely redistributable Freedoom: Phase 2 IWAD. No NES ROM or commercial Doom IWAD is required or included in this repository.

## Project status

> [!WARNING]
> This port is under active development and it is not a playable release yet.

The supported target is the New Nintendo 3DS family: New Nintendo 3DS, New Nintendo 3DS XL and New Nintendo 2DS XL. The native render target is 400×240 on the top screen. A stable 30 FPS is the initial performance goal

## Implemented foundation

- self-contained CIA packaging with GZDoom, Freedoom and Legend of Doom data in RomFS
- Homebrew Launcher 3DSX packaging with an external SD data directory
- Circle Pad, C-Stick, touch-screen and New 3DS shoulder-button input
- OpenAL Soft/NDSP audio path and ZMusic Lite integration
- NovaGL/Citro3D world rendering at 400×240
- reproducible pinned dependency and patch stacks
- physical-hardware startup, GPU, frame-time and crash diagnostics
- GitHub Actions builds with package hashes and public-boundary checks

These items describe implemented code, not a claim that the game is currently playable on hardware.

## Community

Join the Discord for project updates, testing, support and other Nintendo 3DS homebrew projects:

https://discord.gg/SMW49UMkw

## Installation

There is currently no supported public download.

When a release exists, the CIA will be installable through FBI and will not need an external game-data directory. 
The 3DSX package will include a matching SD ZIP that expands to:

```text
sdmc:/3ds/legend-of-doom/
├── legend-of-doom-3ds.3dsx
├── README.txt
├── CREDITS.md
├── THIRD-PARTY-LICENSES.md
├── licenses/
└── data/
    ├── LegendOfDoom.pk3
    ├── freedoom2.wad
    ├── game_support.pk3
    └── gzdoom.pk3
```

Audio on real hardware requires the console's own DSP firmware at `sdmc:/3ds/dspfirm.cdc`. With Luma3DS, open the Rosalina menu, choose **Miscellaneous options...**, then **Dump DSP firmware**. This system file is never distributed with the port.

## Controls

| Nintendo 3DS input | Action |
|---|---|
| Circle Pad | Move / strafe |
| C-Stick | Look |
| Touch screen | Look |
| A | Use / interact |
| B | Attack |
| X | Alternate attack |
| Y | Jump |
| L / R | Previous / next weapon |
| ZL / ZR | Alternate attack / attack |
| D-Pad left / right | Previous / next inventory item |
| D-Pad down | Use inventory item |
| D-Pad up | Automap |
| START | Pause |
| SELECT | Main menu |

## Building

Requirements:

- devkitPro, devkitARM, libctru and 3ds-cmake
- CMake, Git, cURL and UnZip
- SDL2 development files for native host tools on Linux
- `makerom` and `bannertool` for CIA packaging; pinned Linux x86_64 binaries are downloaded automatically, while native `makerom-macos` and `bannertool-macos` are detected on macOS

Build all packages with:

```sh
./platform/3ds/build.sh
```

Build the silent first-frame hardware diagnostic with:

```sh
LOD3DS_BUILD_PROFILE=hardware-diagnostic ./platform/3ds/build.sh
```

The scripts download pinned SDL2, ZMusic, minimp3, OpenAL Soft/NDSP, Legend of Doom and Freedoom inputs; apply the documented 3DS patches; build the executable; and write packages below `build-3ds/dist/`.

No ROM, commercial IWAD, save or crash dump is read during the build. See [platform/3ds/README.md](platform/3ds/README.md) for the renderer contract, diagnostic format and complete build options.

## Credits

- [DeTwelve Games](https://youtube.com/DeTwelveGames) — creator of Legend of Doom
- [GZDoom contributors](https://github.com/ZDoom/gzdoom) — engine
- [Freedoom contributors](https://github.com/freedoom/freedoom) — free Phase 2 IWAD
- [NovaGL contributors](https://github.com/efimandreev0/NovaGL) — OpenGL-to-Citro3D bridge
- SDL, ZMusic, OpenAL Soft/NDSP, devkitPro and libctru contributors — platform stack
- Esteban PDN — Nintendo 3DS port and project maintenance

See [CREDITS.md](CREDITS.md) and [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) for additional notices.

## License and legal notice

The GZDoom-derived source is distributed under GPL-3.0. See [LICENSE](LICENSE).

Third-party code and data retain their respective terms. Legend of Doom's upstream repository does not declare a general license; its data is downloaded only while building and is not committed here. Redistribution must be reviewed separately before any release is published.

*The Legend of Zelda* and related names and content are owned by Nintendo. *Doom* and related names are owned by id Software and their respective rights holders. This is an unofficial fan-made project and is not affiliated with or endorsed by Nintendo, id Software or Bethesda.
