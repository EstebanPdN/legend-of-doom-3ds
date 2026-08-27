# Third-party licenses and notices

This repository is a Nintendo 3DS adaptation of GZDoom 4.7.1. The main source tree is covered by GPL-3.0 and the component-specific notices already stored under `docs/licenses/` and individual library directories.

## Runtime components

| Component | Version / revision | Use | Terms |
|---|---:|---|---|
| GZDoom | 4.7.1, base revision `107ff702423686414680d6458fea63a2647692c4` | Engine | GPL-3.0 and component-specific notices in this tree |
| SDL2 | 2.32.10, revision `5d249570393f7a37e037abf22cd6012a4cc56a71` | Nintendo 3DS platform layer | zlib license |
| ZMusic Lite | 1.1.8, revision `2b291705f2043f39d219a49c2671c80f1dd422e0` | Music decoding interface | LGPL-2.1 with bundled-component notices |
| [minimp3](https://github.com/lieff/minimp3) | revision `ea99364f61c14656440e8d77e9c233ccf3124633` | Static MP3 decoding for ZMusic Lite | CC0-1.0 |
| [NovaGL](https://github.com/efimandreev0/NovaGL) | revision `9cabf853fb57a1037bea55dbec81eea073b5ee6c` | OpenGL-to-Citro3D/PICA200 translation layer | Upstream README declares MIT; authors and contributors are credited there |
| [OpenAL Soft/NDSP](https://github.com/efimandreev0/openal-soft-3ds) | revision `35420d558a001660140033aa70eeee88b0224f3a` | Nintendo 3DS sound backend | LGPL-2.0-or-later; LGPL text under `docs/licenses/` |
| libctru | devkitPro package | Nintendo 3DS system API | zlib license |
| Freedoom: Phase 2 | 0.13.0 | Free Doom-compatible IWAD | BSD-3-Clause; the SD package includes `FREEDOOM-COPYING.txt` |

The build downloads source dependencies from their official repositories and applies the compatibility patches under `platform/3ds/patches/`.

The OpenAL Soft checkout bundles {fmt} 11.2.0 and Microsoft GSL under the MIT license. The SD package preserves their exact license notices, the OpenAL Soft LGPL notice, the SDL2 and minimp3 notices, NovaGL's upstream README/license declaration and contributor credits, and the complete ZMusic license directory under `3ds/legend-of-doom/licenses/`.

The pinned NovaGL revision does not contain a standalone `LICENSE` or `COPYING` file. Its README identifies the project license as MIT. The build therefore packages that README verbatim as `NovaGL-README-MIT-NOTICE.md` without inventing an individual copyright attribution.

## Legend of Doom data

Legend of Doom revision `d7c66cf79fa00b112c17ea443fa63121120ff45b` was created by DeTwelve Games. Its upstream repository does not publish a general open-source license. Its data and the work of its contributors remain under their respective rights and notices. The SD package preserves the original `CREDITS` file; this repository does not relicense that content.

Upstream: https://github.com/emawind84/legend-of-doom

## Build-only packaging tools

| Tool | Version | Terms |
|---|---:|---|
| bannertool | 1.2.0 | MIT |
| Project_CTR makerom | 0.19.0 | MIT |

These tools create CIA metadata and are not linked into the game executable.

## Trademarks and game content

Nintendo owns *The Legend of Zelda* and its associated names and content. id Software and their respective rights holders own *Doom* and its associated names and content. This unofficial project is not affiliated with or endorsed by those companies.
