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
BUILD_PROFILE="${LOD3DS_BUILD_PROFILE:-release}"
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
    NOVAGL_NO_DEBUG=ON
    NOVAGL_DRAW_DIAGNOSTICS=0
    NOVAGL_TEXTURE_DIAGNOSTICS=0
    ;;
  hardware-diagnostic)
    HARDWARE_DIAGNOSTIC=ON
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
  *)
    printf 'Unknown LOD3DS_BUILD_PROFILE: %s\n' "${BUILD_PROFILE}" >&2
    printf 'Expected release or hardware-diagnostic.\n' >&2
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
LOD3DS_BUILD_ROOT="${BUILD_ROOT}" LOD3DS_JOBS="${JOBS}" \
  "${ROOT}/platform/3ds/build-openal-soft.sh"

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
  -DSDL_TEST=OFF
cmake --build "${SDL2_BUILD}" --parallel "${JOBS}"
cmake --install "${SDL2_BUILD}"

cmake -S "${ZMUSIC_SOURCE}" -B "${ZMUSIC_BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DDYN_FLUIDSYNTH=OFF \
  -DDYN_MPG123=OFF \
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
  -DNOVAGL_DIAG_TEX_LIMIT="${NOVAGL_TEXTURE_DIAGNOSTICS}" \
  -DNOVAGL_DIAG_TEX_DUMP_RAW=OFF \
  -DNOVAGL_HARDWARE_STAGE_LOG="$([[ "${BUILD_PROFILE}" == "hardware-diagnostic" ]] && printf ON || printf OFF)" \
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
  -DNO_OPENAL=OFF \
  -DDYN_OPENAL=OFF \
  -DOPENAL_INCLUDE_DIR="${OPENAL_PREFIX}/include/AL" \
  -DOPENAL_LIBRARY="${OPENAL_PREFIX}/lib/libopenal.a" \
  -DNO_OPENMP=ON \
  -DNO_STRIP=ON \
  -DLOD3DS_BUILD_ID="${BUILD_ID}" \
  -DLOD3DS_HARDWARE_DIAGNOSTIC="${HARDWARE_DIAGNOSTIC}" \
  -DNOVAGL_INCLUDE_DIR="${NOVAGL_PREFIX}/include" \
  -DNOVAGL_LIBRARY="${NOVAGL_PREFIX}/lib/libNovaGL.a" \
  -DZMUSIC_INCLUDE_DIR="${ZMUSIC_SOURCE}/include" \
  -DZMUSIC_LIBRARIES="${ZMUSIC_BUILD}/source/libzmusiclite.a" \
  -DSDL2_INCLUDE_DIR="${SDL2_PREFIX}/include/SDL2" \
  "-DSDL2_LIBRARY=${SDL2_PREFIX}/lib/libSDL2main.a;${SDL2_PREFIX}/lib/libSDL2.a;ctru;m"
cmake --build "${GAME_BUILD}" \
  --target zdoom gzdoom_pk3 game_support_pk3 --parallel "${JOBS}"

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
"${HOST_GZDOOM_BUILD}/tools/zipdir/zipdir" -df "${MOD_PK3}" "${MOD_SOURCE}"

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
  printf '3dsx_conventional_heap_bytes=%u\n' "$((64 * 1024 * 1024))"
	printf '3dsx_linear_heap_bytes=%u\n' "$((32 * 1024 * 1024))"
	printf 'cia_conventional_heap=automatic\n'
	printf 'cia_system_mode_ext=124MB\n'
	printf 'cia_linear_heap_bytes=%u\n' "$((32 * 1024 * 1024))"
	printf 'novagl_frame_slots=1\n'
	printf 'gles_world_vbo_pipeline=1\n'
	printf 'gpu_completion_boundary=c3d-queue-wait\n'
	printf 'gles_world_vbo_upload=shadowed-ranges\n'
	printf 'gzdoom_bsp_worker=same-core-disabled\n'
	printf 'early_loading_screen=disabled-hardware-vram-safety\n'
	printf 'initial_scanout=owned-exclusively-by-citro3d\n'
	printf 'novagl_near_clip=selective-fflatvertex-cpu\n'
	printf 'novagl_near_clip_layouts=20-byte,24-byte\n'
	printf 'novagl_eye_clip_topologies=triangles,indexed-triangles,fans,strips,quads\n'
	printf 'novagl_eye_clip_fully_behind=early-reject\n'
	printf 'novagl_eye_clip_epsilon=0.0625\n'
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
	printf 'novagl_vertex_ring_bytes_per_slot=%u\n' "$((2 * 1024 * 1024))"
	printf 'novagl_index_ring_bytes_per_slot=%u\n' "$((512 * 1024))"
	printf 'novagl_texture_staging_bytes=%u\n' "$((512 * 1024))"
	printf 'novagl_per_draw_validation=%s\n' "$([[ "${NOVAGL_NO_DEBUG}" == "ON" ]] && printf compiled-out || printf enabled)"
	printf 'novagl_draw_trace_limit=%s\n' "${NOVAGL_DRAW_DIAGNOSTICS}"
	printf 'novagl_texture_trace_limit=%s\n' "${NOVAGL_TEXTURE_DIAGNOSTICS}"
	printf 'novagl_hardware_stage_log=%s\n' "$([[ "${BUILD_PROFILE}" == "hardware-diagnostic" ]] && printf first-three-swaps || printf disabled)"
	printf 'novagl_hardware_watchdog=%s\n' "$([[ "${BUILD_PROFILE}" == "hardware-diagnostic" ]] && printf first-frame-per-draw-2s-timeout || printf disabled)"
	printf 'gzdoom_render_traces=compiled-out\n'
	printf 'frame_telemetry=%s\n' "$([[ "${HARDWARE_DIAGNOSTIC}" == "ON" ]] && printf ram-buffered-csv-720 || printf compiled-out)"
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
