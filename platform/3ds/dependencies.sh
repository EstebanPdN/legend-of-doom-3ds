#!/usr/bin/env bash
# Pinned inputs used by the Nintendo 3DS build and runtime-data package.

readonly LOD3DS_GZDOOM_BASE_REV="107ff702423686414680d6458fea63a2647692c4"

readonly LOD3DS_SDL2_URL="https://github.com/libsdl-org/SDL.git"
readonly LOD3DS_SDL2_REV="5d249570393f7a37e037abf22cd6012a4cc56a71"

readonly LOD3DS_ZMUSIC_URL="https://github.com/ZDoom/ZMusic.git"
readonly LOD3DS_ZMUSIC_REV="2b291705f2043f39d219a49c2671c80f1dd422e0"

# Header-only CC0 MP3 decoder compiled directly into ZMusic Lite.
readonly LOD3DS_MINIMP3_URL="https://github.com/lieff/minimp3.git"
readonly LOD3DS_MINIMP3_REV="ea99364f61c14656440e8d77e9c233ccf3124633"

# OpenGL ES 1.x compatibility layer backed by Citro3D/PICA200. The tracked
# patch removes the one missing upstream translation unit at this revision and
# adds the narrow fixed-function bridge used by GZDoom's GLES renderer.
readonly LOD3DS_NOVAGL_URL="https://github.com/efimandreev0/NovaGL.git"
readonly LOD3DS_NOVAGL_REV="9cabf853fb57a1037bea55dbec81eea073b5ee6c"

readonly LOD3DS_OPENAL_URL="https://github.com/efimandreev0/openal-soft-3ds.git"
readonly LOD3DS_OPENAL_REV="35420d558a001660140033aa70eeee88b0224f3a"

readonly LOD3DS_MOD_URL="https://github.com/emawind84/legend-of-doom.git"
readonly LOD3DS_MOD_REV="d7c66cf79fa00b112c17ea443fa63121120ff45b"

readonly LOD3DS_FREEDOOM_VERSION="0.13.0"
readonly LOD3DS_FREEDOOM_URL="https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip"
readonly LOD3DS_FREEDOOM_SHA256="3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"

# Optional Linux x86_64 CIA packaging tools. Existing tools from PATH or
# LOD3DS_TOOLS_ROOT are preferred; these archives are fetched only as a
# fallback.
readonly LOD3DS_BANNERTOOL_URL="https://github.com/diasurgical/bannertool/releases/download/1.2.0/bannertool.zip"
readonly LOD3DS_BANNERTOOL_SHA256="69768596f836acb3e3aeaa66e47c6ba560dde813c6dfcd33c8afc25fe29b7524"
readonly LOD3DS_MAKEROM_URL="https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip"
readonly LOD3DS_MAKEROM_SHA256="287b809dec064e0ad597e3d272c49ecb7eed41693d5ee6fef9d8a8aa24c2497e"
