#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="$(tr -d '\r\n' < "${ROOT}/platform/3ds/version.txt")"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
BUILD_ROOT="${LOD3DS_BUILD_ROOT:-${ROOT}/build-3ds}"
mkdir -p "${BUILD_ROOT}"
BUILD_ROOT="$(cd "${BUILD_ROOT}" && pwd)"
JOBS="${LOD3DS_JOBS:-4}"
# Physical hardware is the public default. The legacy GPU path remains
# available only through an explicit LOD3DS_BUILD_PROFILE=release request.
BUILD_PROFILE="${LOD3DS_BUILD_PROFILE:-hardware-safe}"
if (( $# > 1 )); then
  printf 'Usage: %s [build-profile]\n' "$0" >&2
  exit 2
fi
if (( $# == 1 )); then
  BUILD_PROFILE="$1"
fi
SAFE_SOFTWARE=OFF
SAFE_SOFTWARE_SILENT=OFF
HYBRID_PERFORMANCE=OFF
GAME_NO_OPENAL=OFF
SDL_AUDIO=ON
DEPS_ROOT="${BUILD_ROOT}/_deps"
HOST_ZMUSIC_BUILD="${BUILD_ROOT}/host-zmusic"
HOST_GZDOOM_BUILD="${BUILD_ROOT}/host-gzdoom"
SDL2_BUILD="${BUILD_ROOT}/sdl2-3ds"
SDL2_PREFIX="${BUILD_ROOT}/sdl2-3ds-prefix"
OPENAL_PREFIX="${BUILD_ROOT}/openal-soft-3ds-prefix"
ZMUSIC_BUILD="${BUILD_ROOT}/zmusic-3ds"
NOVAGL_BUILD="${BUILD_ROOT}/novagl-3ds"
NOVAGL_PREFIX="${BUILD_ROOT}/novagl-3ds-prefix"
GAME_BUILD="${BUILD_ROOT}/game"
DIST="${BUILD_ROOT}/dist"
TOOLS_CACHE="${BUILD_ROOT}/packaging-tools"
TOOLS_ROOT="${LOD3DS_TOOLS_ROOT:-${ROOT}/../Tools/bin}"

# iCloud Drive may resurrect deleted staging files as "name 2.ext" while a
# package is being assembled, which once duplicated every WAD/PK3 in RomFS and
# inflated the CIA from ~45 MiB to ~85 MiB. Keep all ephemeral package trees
# outside the synced workspace and remove them automatically on exit.
PACKAGE_TMP="$(mktemp -d "${TMPDIR:-/tmp}/lod3ds-package.XXXXXX")"
STAGE="${PACKAGE_TMP}/stage"
trap 'cmake -E remove_directory "${PACKAGE_TMP}"' EXIT

# shellcheck source=dependencies.sh
source "${ROOT}/platform/3ds/dependencies.sh"

export DEVKITPRO DEVKITARM

require_program() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'Required program not found: %s\n' "$1" >&2
    exit 1
  fi
}

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

sha256_stream() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum | awk '{print $1}'
  else
    shasum -a 256 | awk '{print $1}'
  fi
}

source_tree_hash() {
  (
    cd "${ROOT}"
    git ls-files -z --cached --others --exclude-standard | sort -z | \
      while IFS= read -r -d '' path; do
        printf '%s\0' "${path}"
        if [[ -f "${path}" ]]; then
          sha256_file "${path}"
        else
          printf 'deleted\n'
        fi
      done
  ) | sha256_stream
}

download_checked() {
  local url="$1"
  local expected="$2"
  local output="$3"
  local actual=""

  if [[ -f "${output}" ]]; then
    actual="$(sha256_file "${output}")"
  fi
  if [[ "${actual}" != "${expected}" ]]; then
    curl -L --fail --retry 3 --silent --show-error "${url}" -o "${output}.part"
    actual="$(sha256_file "${output}.part")"
    if [[ "${actual}" != "${expected}" ]]; then
      printf 'SHA-256 mismatch for %s\n' "${url}" >&2
      exit 1
    fi
    mv "${output}.part" "${output}"
  fi
}

resolve_tool() {
  local explicit_path="$1"
  local tool_name="$2"
  if [[ -n "${explicit_path}" && -x "${explicit_path}" ]]; then
    printf '%s\n' "${explicit_path}"
  elif [[ "$(uname -s)" == "Darwin" && -x "${TOOLS_ROOT}/${tool_name}-macos" ]]; then
    printf '%s\n' "${TOOLS_ROOT}/${tool_name}-macos"
  elif [[ "$(uname -s)" == "Darwin" && -x "${TOOLS_CACHE}/bin/${tool_name}-macos" ]]; then
    printf '%s\n' "${TOOLS_CACHE}/bin/${tool_name}-macos"
  elif [[ -x "${TOOLS_ROOT}/${tool_name}" ]]; then
    printf '%s\n' "${TOOLS_ROOT}/${tool_name}"
  elif command -v "${tool_name}" >/dev/null 2>&1; then
    command -v "${tool_name}"
  elif [[ -x "${TOOLS_CACHE}/bin/${tool_name}" ]]; then
    printf '%s\n' "${TOOLS_CACHE}/bin/${tool_name}"
  fi
}

prepare_linux_packaging_tools() {
  local banner_archive="${TOOLS_CACHE}/bannertool.zip"
  local makerom_archive="${TOOLS_CACHE}/makerom.zip"
  if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    return
  fi
  mkdir -p "${TOOLS_CACHE}/bin"
  download_checked "${LOD3DS_BANNERTOOL_URL}" "${LOD3DS_BANNERTOOL_SHA256}" "${banner_archive}"
  download_checked "${LOD3DS_MAKEROM_URL}" "${LOD3DS_MAKEROM_SHA256}" "${makerom_archive}"
  unzip -p "${banner_archive}" linux-x86_64/bannertool > "${TOOLS_CACHE}/bin/bannertool"
  unzip -p "${makerom_archive}" makerom > "${TOOLS_CACHE}/bin/makerom"
  chmod +x "${TOOLS_CACHE}/bin/bannertool" "${TOOLS_CACHE}/bin/makerom"
}

for program in cmake git curl unzip awk; do
  require_program "${program}"
done
case "${BUILD_PROFILE}" in
  release)
    HARDWARE_DIAGNOSTIC=OFF
    HARDWARE_DIAGNOSTIC_SILENT=OFF
    NOVAGL_NO_DEBUG=ON
    NOVAGL_DRAW_DIAGNOSTICS=0
    NOVAGL_TEXTURE_DIAGNOSTICS=0
    ;;
  hardware-diagnostic)
    HARDWARE_DIAGNOSTIC=ON
    HARDWARE_DIAGNOSTIC_SILENT=ON
    # L+R+A, screenshots, telemetry, engine state and the full memory map stay
    # enabled, but per-draw validation/probe logging does not. The latter was
    # executing inside thousands of draw calls and made a diagnostic build a
    # poor performance test in its own right. Explicit environment overrides
    # can still re-enable a bounded NovaGL capture when a renderer probe is
    # specifically needed.
    NOVAGL_NO_DEBUG="${NOVAGL_NO_DEBUG:-ON}"
    NOVAGL_DRAW_DIAGNOSTICS="${NOVAGL_DRAW_DIAGNOSTICS:-0}"
    NOVAGL_TEXTURE_DIAGNOSTICS="${NOVAGL_TEXTURE_DIAGNOSTICS:-0}"
    ;;
  hardware-candidate)
    HARDWARE_DIAGNOSTIC=ON
    HARDWARE_DIAGNOSTIC_SILENT=OFF
    # Exercise the actual NDSP/ZMusic path while retaining the bounded
    # physical-console telemetry and first-frame watchdog.
    NOVAGL_NO_DEBUG="${NOVAGL_NO_DEBUG:-ON}"
    NOVAGL_DRAW_DIAGNOSTICS="${NOVAGL_DRAW_DIAGNOSTICS:-0}"
    NOVAGL_TEXTURE_DIAGNOSTICS="${NOVAGL_TEXTURE_DIAGNOSTICS:-0}"
    ;;
  hardware-safe)
    # Physical-hardware baseline: GZDoom renders at 320x200 on the CPU and
    # SDL/libctru presents it to the 400x240 LCD. Audio uses the single pinned
    # OpenAL Soft/NDSP path; SDL audio stays out to avoid duplicate DSP owners.
    HARDWARE_DIAGNOSTIC=OFF
    HARDWARE_DIAGNOSTIC_SILENT=OFF
    SAFE_SOFTWARE=ON
    SAFE_SOFTWARE_SILENT=OFF
    GAME_NO_OPENAL=OFF
    SDL_AUDIO=OFF
    NOVAGL_NO_DEBUG=ON
    NOVAGL_DRAW_DIAGNOSTICS=0
    NOVAGL_TEXTURE_DIAGNOSTICS=0
    ;;
  hardware-hybrid)
    # Stable GZDoom software scene renderer, split across CPU0/core2. Gameplay
    # defaults to 320x192 and can select 200x120 or 400x240; one bilinear
    # textured quad presents it while menus use 400x240. NovaGL never receives
    # world geometry.
    HARDWARE_DIAGNOSTIC=OFF
    HARDWARE_DIAGNOSTIC_SILENT=OFF
    SAFE_SOFTWARE=ON
    SAFE_SOFTWARE_SILENT=OFF
    HYBRID_PERFORMANCE=ON
    GAME_NO_OPENAL=OFF
    SDL_AUDIO=OFF
    NOVAGL_NO_DEBUG=ON
    NOVAGL_DRAW_DIAGNOSTICS=0
    NOVAGL_TEXTURE_DIAGNOSTICS=0
    ;;
  *)
    printf 'Unknown LOD3DS_BUILD_PROFILE: %s\n' "${BUILD_PROFILE}" >&2
    printf 'Expected release, hardware-candidate, hardware-diagnostic, hardware-safe or hardware-hybrid.\n' >&2
    exit 1
    ;;
esac

SOURCE_STATE_SHA256="$(source_tree_hash)"
BUILD_ID="${SOURCE_STATE_SHA256:0:12}"
ARTIFACT_SUFFIX="-${BUILD_PROFILE}-${BUILD_ID}"
ARTIFACT_STEM="legend-of-doom-3ds-v${VERSION}${ARTIFACT_SUFFIX}"
GIT_COMMIT="$(git -C "${ROOT}" rev-parse HEAD)"
GIT_STATUS="clean"
if [[ -n "$(git -C "${ROOT}" status --porcelain)" ]]; then
  GIT_STATUS="dirty"
fi

test -f "${DEVKITPRO}/cmake/3DS.cmake"
test -x "${DEVKITARM}/bin/arm-none-eabi-gcc"
mkdir -p "${BUILD_ROOT}" "${DIST}"

LOD3DS_BUILD_ROOT="${BUILD_ROOT}" "${ROOT}/platform/3ds/fetch-dependencies.sh"
if [[ "${GAME_NO_OPENAL}" != "ON" ]]; then
  LOD3DS_BUILD_ROOT="${BUILD_ROOT}" LOD3DS_JOBS="${JOBS}" \
    "${ROOT}/platform/3ds/build-openal-soft.sh"
fi

SDL2_SOURCE="${DEPS_ROOT}/SDL2"
ZMUSIC_SOURCE="${DEPS_ROOT}/ZMusic"
OPENAL_SOURCE="${LOD3DS_OPENAL_SOURCE_DIR:-${DEPS_ROOT}/openal-soft-3ds}"
MINIMP3_SOURCE="${DEPS_ROOT}/minimp3"
NOVAGL_SOURCE="${DEPS_ROOT}/NovaGL"
MOD_SOURCE="${DEPS_ROOT}/legend-of-doom"
FREEDOOM_SOURCE="${DEPS_ROOT}/freedoom-${LOD3DS_FREEDOOM_VERSION}/freedoom-${LOD3DS_FREEDOOM_VERSION}"

cmake -S "${ZMUSIC_SOURCE}" -B "${HOST_ZMUSIC_BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DDYN_FLUIDSYNTH=OFF \
  -DDYN_MPG123=OFF \
  -DDYN_SNDFILE=OFF
cmake --build "${HOST_ZMUSIC_BUILD}" --target zmusic --parallel "${JOBS}"

HOST_PKG_CONFIG_ARG=""
if [[ -x /usr/bin/pkg-config ]]; then
  HOST_PKG_CONFIG_ARG="-DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config"
fi
cmake -S "${ROOT}" -B "${HOST_GZDOOM_BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DNO_GTK=ON \
  -DNO_OPENAL=ON \
  -DNO_OPENMP=ON \
  -DZMUSIC_INCLUDE_DIR="${ZMUSIC_SOURCE}/include" \
  -DZMUSIC_LIBRARIES="${HOST_ZMUSIC_BUILD}/source/libzmusic.a" \
  ${HOST_PKG_CONFIG_ARG:+"${HOST_PKG_CONFIG_ARG}"}
cmake --build "${HOST_GZDOOM_BUILD}" --target re2c lemon zipdir --parallel "${JOBS}"

if cmake --build "${HOST_GZDOOM_BUILD}" --target help | grep -Eq '(^|[[:space:]])arithchk($|[[:space:]])'; then
  cmake --build "${HOST_GZDOOM_BUILD}" --target arithchk qnan --parallel "${JOBS}"
else
  HOST_CC="${CC:-cc}"
  require_program "${HOST_CC}"
  mkdir -p "${HOST_GZDOOM_BUILD}/libraries/gdtoa"
  (
    cd "${HOST_GZDOOM_BUILD}/libraries/gdtoa"
    "${HOST_CC}" -DINFNAN_CHECK -DMULTIPLE_THREADS \
      "${ROOT}/libraries/gdtoa/arithchk.c" -o arithchk
    ./arithchk > arith.h
    "${HOST_CC}" -DINFNAN_CHECK -DMULTIPLE_THREADS -I. \
      "${ROOT}/libraries/gdtoa/qnan.c" -o qnan
  )
fi

HOST_IMPORTS="${HOST_GZDOOM_BUILD}/ImportExecutables-3ds.cmake"
cmake \
  -DLOD3DS_OUTPUT_FILE="${HOST_IMPORTS}" \
  -DLOD3DS_RE2C="${HOST_GZDOOM_BUILD}/tools/re2c/re2c" \
  -DLOD3DS_LEMON="${HOST_GZDOOM_BUILD}/tools/lemon/lemon" \
  -DLOD3DS_ZIPDIR="${HOST_GZDOOM_BUILD}/tools/zipdir/zipdir" \
  -DLOD3DS_ARITHCHK="${HOST_GZDOOM_BUILD}/libraries/gdtoa/arithchk" \
  -DLOD3DS_QNAN="${HOST_GZDOOM_BUILD}/libraries/gdtoa/qnan" \
  -P "${ROOT}/platform/3ds/configure-host-tools.cmake"

cmake -S "${SDL2_SOURCE}" -B "${SDL2_BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_INSTALL_PREFIX="${SDL2_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_AUDIO="${SDL_AUDIO}" \
  -DSDL_TEST=OFF
cmake --build "${SDL2_BUILD}" --parallel "${JOBS}"
cmake --install "${SDL2_BUILD}"

cmake -S "${ZMUSIC_SOURCE}" -B "${ZMUSIC_BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DDYN_FLUIDSYNTH=OFF \
  -DDYN_MPG123=OFF \
  -DUSE_MPG123=OFF \
  -DDYN_SNDFILE=OFF \
  -DMINIMP3_INCLUDE_DIR="${DEPS_ROOT}/minimp3"
cmake --build "${ZMUSIC_BUILD}" --target zmusiclite --parallel "${JOBS}"

cmake -S "${NOVAGL_SOURCE}" -B "${NOVAGL_BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_INSTALL_PREFIX="${NOVAGL_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DGL2CITRO3D_BUILD_EXAMPLES=OFF \
  -DNOVAGL_GZDOOM_COMPAT=ON \
  -DNOVAGL_NO_DEBUG="${NOVAGL_NO_DEBUG}" \
  -DNOVAGL_DIAG_DRAW_LIMIT="${NOVAGL_DRAW_DIAGNOSTICS}" \
  -DNOVAGL_DIAG_DRAW_START="${NOVAGL_DRAW_DIAGNOSTICS_START:-0}" \
  -DNOVAGL_DIAG_DRAW_CUTOFF="${NOVAGL_DRAW_CUTOFF:--1}" \
  -DNOVAGL_DIAG_SEGMENT_START="${NOVAGL_DIAG_SEGMENT_START:-0}" \
  -DNOVAGL_DIAG_SEGMENT_END="${NOVAGL_DIAG_SEGMENT_END:-0}" \
  -DNOVAGL_DIAG_SEGMENT_STRIDE="${NOVAGL_DIAG_SEGMENT_STRIDE:-0}" \
  -DNOVAGL_DIAG_TEX_LIMIT="${NOVAGL_TEXTURE_DIAGNOSTICS}" \
  -DNOVAGL_DIAG_TEX_DUMP_RAW=OFF \
  -DNOVAGL_HARDWARE_STAGE_LOG="$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf ON || printf OFF)" \
  -DNOVAGL_SPLASHSCREEN=OFF \
  -DNOVAGL_FRAME_BUFFERS=1 \
  -DNOVAGL_SPEEDHACKS=ON
cmake --build "${NOVAGL_BUILD}" --target NovaGL --parallel "${JOBS}"
cmake --install "${NOVAGL_BUILD}"

cmake -S "${ROOT}" -B "${GAME_BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DIMPORT_EXECUTABLES="${HOST_IMPORTS}" \
  -DHAVE_VULKAN=OFF \
  -DNO_GTK=ON \
  -DNO_OPENAL="${GAME_NO_OPENAL}" \
  -DDYN_OPENAL=OFF \
  -DOPENAL_INCLUDE_DIR="${OPENAL_PREFIX}/include/AL" \
  -DOPENAL_LIBRARY="${OPENAL_PREFIX}/lib/libopenal.a" \
  -DNO_OPENMP=ON \
  -DNO_STRIP=ON \
  -DLOD3DS_BUILD_PROFILE="${BUILD_PROFILE}" \
  -DLOD3DS_BUILD_ID="${BUILD_ID}" \
  -DLOD3DS_PORT_VERSION="${VERSION}" \
  -DLOD3DS_HARDWARE_DIAGNOSTIC="${HARDWARE_DIAGNOSTIC}" \
  -DLOD3DS_HARDWARE_DIAGNOSTIC_SILENT="${HARDWARE_DIAGNOSTIC_SILENT}" \
  -DLOD3DS_SAFE_SOFTWARE="${SAFE_SOFTWARE}" \
  -DLOD3DS_SAFE_SOFTWARE_SILENT="${SAFE_SOFTWARE_SILENT}" \
  -DLOD3DS_HYBRID_PERFORMANCE="${HYBRID_PERFORMANCE}" \
  -DNOVAGL_INCLUDE_DIR="${NOVAGL_PREFIX}/include" \
  -DNOVAGL_LIBRARY="${NOVAGL_PREFIX}/lib/libNovaGL.a" \
  -DZMUSIC_INCLUDE_DIR="${ZMUSIC_SOURCE}/include" \
  -DZMUSIC_LIBRARIES="${ZMUSIC_BUILD}/source/libzmusiclite.a" \
  -DSDL2_INCLUDE_DIR="${SDL2_PREFIX}/include/SDL2" \
  "-DSDL2_LIBRARY=${SDL2_PREFIX}/lib/libSDL2main.a;${SDL2_PREFIX}/lib/libSDL2.a;ctru;m"
cmake --build "${GAME_BUILD}" \
  --target zdoom gzdoom_pk3 game_support_pk3 --parallel "${JOBS}"

if [[ "${BUILD_PROFILE}" == "hardware-safe" ]]; then
  SAFE_SYMBOLS="$("${DEVKITARM}/bin/arm-none-eabi-nm" -C "${GAME_BUILD}/gzdoom.elf")"
  if grep -Eq ' N3DSAUDIO_Init$' <<<"${SAFE_SYMBOLS}"; then
    printf 'Hardware-safe ELF unexpectedly contains the SDL N3DS audio backend.\n' >&2
    exit 1
  fi
  for required_symbol in alcOpenDevice ndspInit __system_allocateHeaps; do
    if ! grep -Eq " [TW] ${required_symbol}$" <<<"${SAFE_SYMBOLS}"; then
      printf 'Hardware-safe ELF is missing required strong symbol: %s\n' "${required_symbol}" >&2
      exit 1
    fi
  done
  for forbidden_symbol in I_PolyPresentDirect3DS nova_init_ex novaSwapBuffers novaSetSwapInterval; do
    if grep -q "${forbidden_symbol}" <<<"${SAFE_SYMBOLS}"; then
      printf 'Hardware-safe ELF contains forbidden experimental entry point: %s\n' \
        "${forbidden_symbol}" >&2
      exit 1
    fi
  done
  OPENAL_SYMBOLS="$("${DEVKITARM}/bin/arm-none-eabi-nm" -A "${OPENAL_PREFIX}/lib/libopenal.a")"
  if ! grep -q 'svcStoreProcessDataCache' <<<"${OPENAL_SYMBOLS}"; then
    printf 'Hardware-safe OpenAL archive is missing the low-jitter cache clean.\n' >&2
    exit 1
  fi
  if ! grep -q 'DSP_FlushDataCache' <<<"${OPENAL_SYMBOLS}"; then
    printf 'Hardware-safe OpenAL archive is missing the compatibility cache-clean fallback.\n' >&2
    exit 1
  fi
elif [[ "${BUILD_PROFILE}" == "hardware-hybrid" ]]; then
  HYBRID_SYMBOLS="$("${DEVKITARM}/bin/arm-none-eabi-nm" -C "${GAME_BUILD}/gzdoom.elf")"
  for required_symbol in I_PolyPresentDirect3DS C3D_SyncDisplayTransfer C2D_DrawImage threadCreate alcOpenDevice ndspInit; do
    if ! grep -q "${required_symbol}" <<<"${HYBRID_SYMBOLS}"; then
      printf 'Hardware-hybrid ELF is missing required symbol: %s\n' "${required_symbol}" >&2
      exit 1
    fi
  done
  for forbidden_symbol in nova_init_ex novaSwapBuffers novaSetSwapInterval; do
    if grep -q "${forbidden_symbol}" <<<"${HYBRID_SYMBOLS}"; then
      printf 'Hardware-hybrid ELF unexpectedly contains NovaGL world entry point: %s\n' \
        "${forbidden_symbol}" >&2
      exit 1
    fi
  done
  OPENAL_SYMBOLS="$("${DEVKITARM}/bin/arm-none-eabi-nm" -A "${OPENAL_PREFIX}/lib/libopenal.a")"
  for required_symbol in svcStoreProcessDataCache DSP_FlushDataCache; do
    if ! grep -q "${required_symbol}" <<<"${OPENAL_SYMBOLS}"; then
      printf 'Hardware-hybrid OpenAL archive is missing audio-stability symbol: %s\n' \
        "${required_symbol}" >&2
      exit 1
    fi
  done
fi

THREEDSXTOOL="${DEVKITPRO}/tools/bin/3dsxtool"
SMDHTOOL="${DEVKITPRO}/tools/bin/smdhtool"
if [[ ! -x "${THREEDSXTOOL}" ]]; then THREEDSXTOOL="$(command -v 3dsxtool)"; fi
if [[ ! -x "${SMDHTOOL}" ]]; then SMDHTOOL="$(command -v smdhtool)"; fi
SMDH="${GAME_BUILD}/legend-of-doom-3ds.smdh"
THREEDSX="${DIST}/${ARTIFACT_STEM}.3dsx"
"${SMDHTOOL}" --create \
  "Legend of Doom 3DS v${VERSION}" \
  "Legend of Doom for Nintendo 3DS" \
  "Esteban PDN / DeTwelve Games" \
  "${ROOT}/platform/3ds/assets/icon-48.png" "${SMDH}"
"${THREEDSXTOOL}" "${GAME_BUILD}/gzdoom.elf" "${THREEDSX}" --smdh="${SMDH}"

MOD_PK3="${BUILD_ROOT}/LegendOfDoom.pk3"
cmake -E rm -f "${MOD_PK3}"
MOD_PACKAGE_SOURCE="${BUILD_ROOT}/legend-of-doom-package"
cmake -E remove_directory "${MOD_PACKAGE_SOURCE}"
cmake -E copy_directory "${MOD_SOURCE}" "${MOD_PACKAGE_SOURCE}"
cmake -E copy "${ROOT}/platform/3ds/assets/menu-top.png" \
  "${MOD_PACKAGE_SOURCE}/graphics/TITLEPIC.png"
cmake -E copy "${ROOT}/platform/3ds/assets/menu-story.png" \
  "${MOD_PACKAGE_SOURCE}/graphics/ZSTORY.png"
"${HOST_GZDOOM_BUILD}/tools/zipdir/zipdir" -df "${MOD_PK3}" "${MOD_PACKAGE_SOURCE}"

cmake -E remove_directory "${STAGE}"
SD_APP="${STAGE}/3ds/legend-of-doom"
SD_DATA="${SD_APP}/data"
SD_LICENSES="${SD_APP}/licenses"
mkdir -p "${SD_DATA}" "${SD_LICENSES}/ZMusic"
cmake -E copy "${THREEDSX}" "${SD_APP}/legend-of-doom-3ds.3dsx"
cmake -E copy "${ROOT}/platform/3ds/SD-README.txt" "${SD_APP}/README.txt"
cmake -E copy "${ROOT}/CREDITS.md" "${SD_APP}/CREDITS.md"
cmake -E copy "${ROOT}/THIRD-PARTY-LICENSES.md" "${SD_APP}/THIRD-PARTY-LICENSES.md"
cmake -E copy "${ROOT}/LICENSE" "${SD_LICENSES}/GPL-3.0.txt"
cmake -E copy "${SDL2_SOURCE}/LICENSE.txt" "${SD_LICENSES}/SDL2-LICENSE.txt"
cmake -E copy_directory "${ZMUSIC_SOURCE}/licenses" "${SD_LICENSES}/ZMusic"
cmake -E copy "${OPENAL_SOURCE}/COPYING" "${SD_LICENSES}/OpenAL-Soft-COPYING.txt"
cmake -E copy "${OPENAL_SOURCE}/fmt-11.2.0/LICENSE" "${SD_LICENSES}/fmt-LICENSE.txt"
cmake -E copy "${OPENAL_SOURCE}/gsl/LICENSE" "${SD_LICENSES}/Microsoft-GSL-LICENSE.txt"
cmake -E copy "${MINIMP3_SOURCE}/LICENSE" "${SD_LICENSES}/minimp3-LICENSE.txt"
cmake -E copy "${NOVAGL_SOURCE}/README.md" "${SD_LICENSES}/NovaGL-README-MIT-NOTICE.md"
cmake -E copy "${GAME_BUILD}/gzdoom.pk3" "${SD_DATA}/gzdoom.pk3"
cmake -E copy "${GAME_BUILD}/game_support.pk3" "${SD_DATA}/game_support.pk3"
cmake -E copy "${MOD_PK3}" "${SD_DATA}/LegendOfDoom.pk3"
cmake -E copy "${FREEDOOM_SOURCE}/freedoom2.wad" "${SD_DATA}/freedoom2.wad"
cmake -E copy "${FREEDOOM_SOURCE}/COPYING.txt" "${SD_DATA}/FREEDOOM-COPYING.txt"
cmake -E copy "${MOD_SOURCE}/CREDITS" "${SD_DATA}/LEGEND-OF-DOOM-CREDITS.txt"

BUILD_MANIFEST="${DIST}/BUILD-MANIFEST.txt"
ARM_SIZE="${DEVKITARM}/bin/arm-none-eabi-size"
{
  printf 'Legend of Doom 3DS build manifest\n'
  printf 'format_version=2\n'
  printf 'build_id=%s\n' "${BUILD_ID}"
  printf 'port_version=%s\n' "${VERSION}"
  printf 'profile=%s\n' "${BUILD_PROFILE}"
  printf 'artifact_stem=%s\n' "${ARTIFACT_STEM}"
  printf 'hardware_target=New Nintendo 3DS\n'
  printf 'git_commit=%s\n' "${GIT_COMMIT}"
  printf 'git_status=%s\n' "${GIT_STATUS}"
  printf 'source_state_sha256=%s\n' "${SOURCE_STATE_SHA256}"
  printf 'gzdoom_base=%s\n' "${LOD3DS_GZDOOM_BASE_REV}"
  printf 'sdl2=%s\n' "${LOD3DS_SDL2_REV}"
  printf 'zmusic=%s\n' "${LOD3DS_ZMUSIC_REV}"
  printf 'minimp3=%s\n' "${LOD3DS_MINIMP3_REV}"
  printf 'novagl=%s\n' "${LOD3DS_NOVAGL_REV}"
  printf 'legend_of_doom=%s\n' "${LOD3DS_MOD_REV}"
  printf 'freedoom=%s\n' "${LOD3DS_FREEDOOM_VERSION}"
	printf 'cia_runtime_data=embedded-romfs\n'
	printf '3dsx_runtime_data=sdmc:/3ds/legend-of-doom/data\n'
	printf 'gzdoom_pk3_sha256=%s\n' "$(sha256_file "${GAME_BUILD}/gzdoom.pk3")"
	printf 'game_support_pk3_sha256=%s\n' "$(sha256_file "${GAME_BUILD}/game_support.pk3")"
	printf 'legend_of_doom_pk3_sha256=%s\n' "$(sha256_file "${MOD_PK3}")"
	printf 'freedoom2_wad_sha256=%s\n' "$(sha256_file "${FREEDOOM_SOURCE}/freedoom2.wad")"
  printf 'compiler=%s\n' "$("${DEVKITARM}/bin/arm-none-eabi-g++" --version | head -n 1)"
  printf 'cmake=%s\n' "$(cmake --version | head -n 1)"
	printf 'runtime_renderer=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf softpoly-core0-core2-pica200-presenter || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf softpoly-sdl-linear-framebuffer || printf novagl-citro3d-pica200))"
	printf 'internal_resolution=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf 200x120-320x192-400x240-touch-selectable-gameplay-plus-400x240-native-menus || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf 320x200-game-400x240-special-ui || printf 400x240))"
	printf 'lcd_resolution=400x240\n'
	printf '3dsx_conventional_heap_bytes=%u\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf %u "$((92 * 1024 * 1024))" || printf %u "$((64 * 1024 * 1024))")"
	printf '3dsx_linear_heap_bytes=%u\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf %u "$((4 * 1024 * 1024))" || printf %u "$((32 * 1024 * 1024))")"
	printf 'cia_conventional_heap=runtime-capped-to-92MiB-with-4MiB-address-guard\n'
	printf 'cia_system_mode_fallback=64MB\n'
	printf 'cia_system_mode_ext=124MB\n'
	printf 'cia_linear_heap=runtime-remainder-min-%u-max-%u\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf %u "$((4 * 1024 * 1024))" || printf %u "$((32 * 1024 * 1024))")" "$((32 * 1024 * 1024))"
	printf 'softpoly_startup_reserve_bytes=%u\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf %u "$((2 * 1024 * 1024))" || printf 0)"
	printf 'softpoly_flat_vertex_capacity=%u\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf 65536 || printf 100000)"
	printf 'apt_exit_supervisor=home-or-close-after-8000ms\n'
	printf 'sdl_scanout_buffers=linear-cpu-writable-rgba8\n'
	printf 'novagl_runtime=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf linked-but-not-initialized || printf active)"
	printf 'novagl_frame_slots=1\n'
	printf 'compiler_optimization=O2-release-toolchain-default\n'
	printf 'render_cap_fps=always-uncapped-display-synchronized-at-60hz\n'
	printf 'drawer_threads=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf 2-explicit-libctru-core0-core2 || printf 1)"
	printf 'scene_threads=1\n'
	printf 'game_tick_hz=35\n'
	printf 'texture_sort=enabled\n'
	printf 'dynamic_lights=disabled\n'
	printf 'actor_sprite_shadows=disabled\n'
	printf 'particles_max=1024\n'
	printf 'particle_style=0\n'
	printf 'portal_recursions=1\n'
	printf 'mirror_recursions=1\n'
	printf 'gles_world_vbo_pipeline=1\n'
	printf 'gpu_completion_boundary=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf citro3d-frame-retirement || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf not-applicable || printf pinned-citro3d-render-queue-wait))"
	printf 'novagl_frame_end=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf citro3d-full-linear-fallback)"
	printf 'novagl_cpu_gpu_cache=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf explicit-ranges-plus-full-frame-fallback)"
	printf 'cpu_to_device_cache_clean=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf gsp-service-v0.6-contract || printf svc-store-with-service-fallback)"
	printf 'audio_core1_policy=v0.6-preserve-existing-or-raise-minimum-to-30-percent\n'
	printf 'audio_core1_minimum_app_share_percent=30\n'
	printf 'novagl_sync_helpers=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf preflush-hidden-splits)"
	printf 'novagl_transfer_staging=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf unique-until-queue-fence)"
	printf 'novagl_mvp=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf combined-cache-on-dirty-transform)"
	printf 'gles_world_vbo_upload=shadowed-ranges\n'
	printf 'gzdoom_bsp_worker=disabled-on-3ds-race-audited\n'
	printf 'software_presenter=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf citro2d-bounded-single-quad-with-sdl-init-fallback || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf sdl-software-v0.6-contract || printf inactive))"
	printf 'software_presenter_sampling=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf pica200-bilinear || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf sdl-16.16-nearest || printf inactive))"
	printf 'software_presenter_color=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf bgra-to-rgb-tev-g-b-alpha-before-draw || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf sdl-owned || printf inactive))"
	printf 'software_presenter_cache_cleans=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf gsp-active-upload-rows-plus-bounded-citro2d-vertex-range || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf owned-by-sdl-libctru || printf inactive))"
	printf 'hardware_safe_gpu_entrypoints=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf not-applicable-hybrid-presenter-only || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf link-time-rejected || printf not-applicable))"
	printf 'hardware_safe_first_present_breadcrumbs=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf first-three-frames || printf inactive)"
	printf 'early_loading_screen=sdl-owned-rgba8-triforce-animation-96x96-33frames\n'
	printf 'initial_scanout=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf owned-by-sdl-libctru || printf owned-exclusively-by-citro3d)"
	printf 'novagl_near_clip=selective-fflatvertex-cpu\n'
	printf 'novagl_near_clip_layouts=20-byte,24-byte\n'
	printf 'novagl_eye_clip_topologies=triangles,indexed-triangles,fans,strips,quads\n'
	printf 'novagl_eye_clip_fully_behind=early-reject\n'
	printf 'novagl_eye_clip_epsilon=0.0625\n'
	printf 'novagl_eye_clip_side_planes=left,right,bottom,top\n'
	printf 'novagl_eye_clip_allocation=exact-two-pass\n'
	printf 'novagl_actor_billboards=indexed-triangles\n'
	printf 'novagl_fflat_fast_path=stable-32k-base-sequential-u16\n'
	printf 'novagl_vram_upload=linear-staging-gx-copy\n'
	printf 'novagl_vram_cpu_writes=forbidden\n'
	printf 'novagl_sampled_texture_storage=linear-cpu-writable\n'
	printf 'novagl_vram_usage=render-targets-only\n'
	printf 'novagl_fast_path_clip_guard=pre-dispatch\n'
	printf 'novagl_user_clip=gzdoom-portal-plane-cpu\n'
	printf 'novagl_depth_clamp=gzdoom-state-aware-cpu-clip\n'
	printf 'novagl_stencil_early_z=disabled-for-side-effects\n'
	printf 'novagl_early_z=disabled-hardware-safe\n'
	printf 'novagl_index_diagnostics=resolved-ebo-indices\n'
	printf 'novagl_command_buffer_bytes=%u\n' "$((3 * 1024 * 1024))"
	printf 'novagl_command_segment_limit_bytes=%u\n' "$((0x40000 * 3 / 4))"
	printf 'novagl_first_frame_segment_limit_bytes=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf %u "$((0x40000 * 3 / 4))" || printf inactive)"
	printf 'novagl_float_safety=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf ieee754-bitwise-fast-math-safe)"
	printf 'novagl_immediate_float_safety=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf drop-nonfinite-batch)"
	printf 'novagl_tev_color_safety=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf validate-clamp-before-u32)"
	printf 'novagl_vertex_range_safety=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf mandatory-vbo-ebo-max-index)"
	printf 'novagl_raster_state_safety=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf empty-viewport-scissor-discard)"
	printf 'novagl_internal_draw_success=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf bufinfo-confirmed)"
	printf 'novagl_scanout_transfer=%s\n' "$([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf inactive || printf rgba8-to-rgba8)"
	printf 'novagl_vertex_ring_bytes_per_slot=%u\n' "$((2 * 1024 * 1024))"
	printf 'novagl_index_ring_bytes_per_slot=%u\n' "$((512 * 1024))"
	printf 'novagl_texture_staging_bytes=%u\n' "$((512 * 1024))"
	printf 'novagl_per_draw_validation=%s\n' "$([[ "${NOVAGL_NO_DEBUG}" == "ON" ]] && printf compiled-out || printf enabled)"
	printf 'novagl_draw_trace_limit=%s\n' "${NOVAGL_DRAW_DIAGNOSTICS}"
	printf 'novagl_draw_cutoff=%s\n' "${NOVAGL_DRAW_CUTOFF:--1}"
	printf 'novagl_segment_probe=%s-%s/stride-%s\n' "${NOVAGL_DIAG_SEGMENT_START:-0}" "${NOVAGL_DIAG_SEGMENT_END:-0}" "${NOVAGL_DIAG_SEGMENT_STRIDE:-0}"
	printf 'novagl_texture_trace_limit=%s\n' "${NOVAGL_TEXTURE_DIAGNOSTICS}"
	printf 'novagl_hardware_stage_log=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf first-three-swaps || printf disabled)"
	printf 'novagl_hardware_watchdog=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf first-frame-progress-5s-stall-timeout || printf disabled)"
	printf 'novagl_stall_command_dump=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf gpu-command-segment.bin || printf disabled)"
	printf 'gzdoom_render_traces=compiled-out\n'
	printf 'frame_telemetry=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf ram-buffered-csv-720 || printf compiled-out)"
	printf 'audio=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC_SILENT}" == "ON" || "${SAFE_SOFTWARE_SILENT}" == "ON" ]] && printf disabled || printf enabled)"
	printf 'audio_backend=%s\n' "$([[ "${GAME_NO_OPENAL}" == "ON" && "${SDL_AUDIO}" == "OFF" ]] && printf compiled-out-no-ndsp || printf openal-ndsp)"
	printf 'audio_cache_clean=process-svc-clean-with-dsp-service-fallback\n'
	printf 'audio_ndsp_queue=8-wave-buffers\n'
	printf 'audio_music_stream=core1-priority-inherited-20ms-refill-8-pcm-buffers\n'
	printf 'audio_gain=sfx-listener-neutral-1x-music-only-3x-plus-200-percent\n'
	printf 'dump_audio=entire-openal-device-paused-with-music-state-restored\n'
	printf 'diagnostic_dump_default=quick-no-memory-payload\n'
	printf 'diagnostic_dump_full=L+R+X-with-memory-payload\n'
	printf 'diagnostic_full_memory=single-atomic-memory-bin-512KiB-no-device-hash\n'
	printf 'diagnostic_dump_clean=L+R+Y-delete-all-dump-files\n'
	printf 'diagnostic_space_guard=measured-payload-plus-8MiB-reserve\n'
	printf 'bottom_interface=gameplay-map-items-native-320x240\n'
	printf 'bottom_interface_default=map-left-items-right-compact-pixel-labels-blue-frame-and-selection-underline\n'
	printf 'bottom_automap=touch-toggle-12-or-32-world-units-per-pixel-explored-floor-textures-plus-toggleable-semantic-collision-lines-plus-exterior-only-player-arrow\n'
	printf 'bottom_items=live-4x4-icons-no-empty-cells-plus-white-rounded-square-selection-and-direct-touch\n'
	printf 'bottom_status=adaptive-three-to-four-row-hearts-46x55-live-mugshot-lower-132-percent-item-counters\n'
	printf 'bottom_developer_overlay=v0.14-select-toggle-performance-memory-status-plus-quick-full-clean-controls\n'
	printf 'menu_top=custom-native-400x240-titlepic-byte-exact-source\n'
	printf 'menu_top_filter=source-native-400x240-no-resample\n'
	printf 'menu_top_source_sha256=%s\n' "$(sha256_file "${ROOT}/platform/3ds/assets/menu-top.png")"
	printf 'menu_runtime_canvas=title-lore-intermission-all-menus-and-console-native-400x240-live-gameplay-and-hud-%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf touch-selectable-200x120-320x192-400x240 || printf fixed-320x200)"
	printf 'menu_story=custom-native-400x240-credit-page-byte-exact-source\n'
	printf 'menu_story_source_sha256=%s\n' "$(sha256_file "${ROOT}/platform/3ds/assets/menu-story.png")"
	printf 'menu_bottom=custom-native-320x240-before-level\n'
	printf 'menu_bottom_story=black-on-credit-page\n'
	printf 'menu_bottom_dim=match-title-menu-72-percent-black\n'
	printf 'menu_story_filter=source-native-400x240-no-resample\n'
	printf 'pause_menu=stable-uppercase-engine-actions-shifted-right-10px-with-selector-lowered-5px-and-touch\n'
	printf 'pause_menu_logo=removed-live-scene-only\n'
	printf 'pause_menu_dim=engine-matched-translucent-gameplay-background\n'
	printf 'options_menu=volume-plus-display-plus-controller-cstick-touch-sprint-controls-reference-plus-developer\n'
	printf 'controls=R-attack-L-altattack-dpad-weapons-ZL-ZR-inventory-X-full-health-sprint\n'
	printf 'fps_counter=final-canvas-top-right-resolution-aware-compact-glyphs-fps-plus-smoothed-frame-milliseconds\n'
	printf 'aim_crosshair=final-canvas-provided-png-70-percent-size-plus-6px-lower-opt-in-all-render-scales\n'
	printf 'upper_automap=full-canvas-64-world-units-per-pixel-textured-overview-distinct-from-bottom-map\n'
	printf 'option_alignment=split-labels-left-values-right-plus-compact-sliders-without-numeric-readouts\n'
	printf 'controls_reference=no-caption-two-column-input-action-newsmallfont-layout-raised-10px\n'
	printf 'save_load=top-only-title-readable-pixel-text-independent-black-bottom-white-section-frame-blue-row-selection-touch-list-top-preview-raised-8px-transparent-matched-width-info\n'
	printf 'save_keyboard=native-3ds-qwerty-confirm-to-save-with-preserved-double-buffered-top-image\n'
	printf 'hud_messages=acs-dialogue-preserved-when-top-hud-disabled-plus-black-shadowed-pickup-notices\n'
	printf 'bottom_automap_level_reset=map-name-aware-dungeon-and-interior-recenter\n'
	printf 'player_name=Link-forced-on-3ds-launch\n'
	printf 'wipe_default=Burn-forced-on-3ds-launch\n'
	printf 'bottom_diagnostics=button-chords-retained-fullscreen-progress-only\n'
	printf 'dump_progress=quick-full-clean-fullscreen\n'
	printf 'dump_screen_capture=one-second-delay-ram-snapshot-before-immediate-progress-ui\n'
	printf 'softpoly_sky=map01-skyww-cloud-texture-no-remote-skyviewpoint-geometry\n'
	printf 'map01_draw_distance=2048-units-line-sprite-cull-with-full-bsp-plane-traversal\n'
	printf 'map01_distance_fog=explicit-smoothstep-1536-to-2048-units-black-cave-sectors-excluded\n'
	printf 'death_filter=transparent-dark-red-alpha-0.16\n'
	printf 'ndsp_lifetime=backend-owned-worker-joined-before-device-destruction\n'
	printf 'software_frame_clear=full-bgra-canvas-four-bytes-per-pixel-each-frame\n'
	printf 'top_scanout=%s\n' "$([[ "${HYBRID_PERFORMANCE}" == "ON" ]] && printf 320x192-game-pica-bilinear-plus-400x240-native-menus || ([[ "${SAFE_SOFTWARE}" == "ON" ]] && printf 320x200-game-sdl-nearest-plus-400x240-menu-sdl || printf novagl-citro3d-400x240))"
  printf '3dsx_sha256=%s\n' "$(sha256_file "${THREEDSX}")"
  printf 'elf_sha256=%s\n' "$(sha256_file "${GAME_BUILD}/gzdoom.elf")"
  printf 'map_sha256=%s\n' "$(sha256_file "${GAME_BUILD}/gzdoom.map")"
  "${ARM_SIZE}" "${GAME_BUILD}/gzdoom.elf"
} > "${BUILD_MANIFEST}"
cmake -E copy "${BUILD_MANIFEST}" "${SD_APP}/BUILD-MANIFEST.txt"

SD_ZIP="${DIST}/${ARTIFACT_STEM}-sd.zip"
cmake -E rm -f "${SD_ZIP}"
(
  cd "${STAGE}"
  cmake -E tar cf "${SD_ZIP}" --format=zip 3ds
)

MAKEROM_PATH="$(resolve_tool "${MAKEROM:-}" makerom || true)"
BANNERTOOL_PATH="$(resolve_tool "${BANNERTOOL:-}" bannertool || true)"
if [[ "${LOD3DS_SKIP_CIA:-0}" != "1" && ( -z "${MAKEROM_PATH}" || -z "${BANNERTOOL_PATH}" ) ]]; then
  prepare_linux_packaging_tools
  MAKEROM_PATH="$(resolve_tool "${MAKEROM:-}" makerom || true)"
  BANNERTOOL_PATH="$(resolve_tool "${BANNERTOOL:-}" bannertool || true)"
fi

CIA="${DIST}/${ARTIFACT_STEM}.cia"
cmake -E rm -f "${CIA}"
if [[ "${LOD3DS_SKIP_CIA:-0}" != "1" && -n "${MAKEROM_PATH}" && -n "${BANNERTOOL_PATH}" ]]; then
  CIA_ROMFS="${PACKAGE_TMP}/cia-romfs"
  CIA_ROMFS_DATA="${CIA_ROMFS}/data"
  mkdir -p "${CIA_ROMFS_DATA}"
  # A CIA installed from QR must be complete on its own. Keeping these files
  # in RomFS also guarantees that the executable and its engine/mod data are
  # from the same source-state ID; configs, saves, logs and dumps remain on SD.
  cmake -E copy "${GAME_BUILD}/gzdoom.pk3" "${CIA_ROMFS_DATA}/gzdoom.pk3"
  cmake -E copy "${GAME_BUILD}/game_support.pk3" "${CIA_ROMFS_DATA}/game_support.pk3"
  cmake -E copy "${MOD_PK3}" "${CIA_ROMFS_DATA}/LegendOfDoom.pk3"
  cmake -E copy "${FREEDOOM_SOURCE}/freedoom2.wad" "${CIA_ROMFS_DATA}/freedoom2.wad"
  cmake -E copy "${BUILD_MANIFEST}" "${CIA_ROMFS}/BUILD-MANIFEST.txt"
  "${BANNERTOOL_PATH}" makebanner \
    -i "${ROOT}/platform/3ds/assets/banner.png" \
    -a "${ROOT}/platform/3ds/assets/banner.wav" \
    -o "${GAME_BUILD}/legend-of-doom-3ds.bnr"
  "${MAKEROM_PATH}" -f cia -o "${CIA}" \
    -DAPP_ROMFS="${CIA_ROMFS}" \
    -rsf "${ROOT}/platform/3ds/cia/legend-of-doom-3ds.rsf" \
    -target t -exefslogo \
    -elf "${GAME_BUILD}/gzdoom.elf" \
    -icon "${SMDH}" \
    -banner "${GAME_BUILD}/legend-of-doom-3ds.bnr"
else
  printf 'CIA packaging skipped; set MAKEROM and BANNERTOOL or install them in PATH.\n'
fi

DEBUG_STAGE="${PACKAGE_TMP}/debug-symbols"
DEBUG_ZIP="${DIST}/${ARTIFACT_STEM}-debug-symbols.zip"
cmake -E remove_directory "${DEBUG_STAGE}"
mkdir -p "${DEBUG_STAGE}"
cmake -E copy "${GAME_BUILD}/gzdoom.elf" "${DEBUG_STAGE}/gzdoom-${BUILD_ID}.elf"
cmake -E copy "${GAME_BUILD}/gzdoom.map" "${DEBUG_STAGE}/gzdoom-${BUILD_ID}.map"
cmake -E copy "${BUILD_MANIFEST}" "${DEBUG_STAGE}/BUILD-MANIFEST.txt"
cmake -E rm -f "${DEBUG_ZIP}"
(
  cd "${DEBUG_STAGE}"
  cmake -E tar cf "${DEBUG_ZIP}" --format=zip .
)

CHECKSUMS="${DIST}/SHA256SUMS.txt"
printf '%s  %s\n' "$(sha256_file "${THREEDSX}")" "$(basename "${THREEDSX}")" > "${CHECKSUMS}"
printf '%s  %s\n' "$(sha256_file "${SD_ZIP}")" "$(basename "${SD_ZIP}")" >> "${CHECKSUMS}"
printf '%s  %s\n' "$(sha256_file "${DEBUG_ZIP}")" "$(basename "${DEBUG_ZIP}")" >> "${CHECKSUMS}"
printf '%s  %s\n' "$(sha256_file "${BUILD_MANIFEST}")" "$(basename "${BUILD_MANIFEST}")" >> "${CHECKSUMS}"
if [[ -f "${CIA}" ]]; then
  printf '%s  %s\n' "$(sha256_file "${CIA}")" "$(basename "${CIA}")" >> "${CHECKSUMS}"
fi

printf 'Ready under %s:\n' "${DIST}"
for artifact in "${BUILD_MANIFEST}" "${CHECKSUMS}" "${DEBUG_ZIP}" "${SD_ZIP}" "${THREEDSX}" "${CIA}"; do
  if [[ -f "${artifact}" ]]; then
    printf '%s\n' "${artifact}"
  fi
done
