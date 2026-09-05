#!/usr/bin/env bash
set -euo pipefail

# Build the Nintendo 3DS OpenAL Soft/NDSP port as an isolated, pinned
# dependency. The game build consumes only the installed headers and archive.

LOD3DS_SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=dependencies.sh
source "${LOD3DS_SOURCE_ROOT}/platform/3ds/dependencies.sh"
LOD3DS_BUILD_ROOT="${LOD3DS_BUILD_ROOT:-${LOD3DS_SOURCE_ROOT}/build-3ds}"
LOD3DS_OPENAL_CHECKOUT="${LOD3DS_OPENAL_SOURCE_DIR:-${LOD3DS_BUILD_ROOT}/_deps/openal-soft-3ds}"
LOD3DS_OPENAL_BUILD_DIR="${LOD3DS_OPENAL_BUILD_DIR:-${LOD3DS_BUILD_ROOT}/openal-soft-3ds}"
LOD3DS_OPENAL_PREFIX="${LOD3DS_OPENAL_PREFIX:-${LOD3DS_BUILD_ROOT}/openal-soft-3ds-prefix}"
LOD3DS_OPENAL_JOBS="${LOD3DS_JOBS:-4}"
readonly LOD3DS_OPENAL_CORE_PATCH="${LOD3DS_SOURCE_ROOT}/platform/3ds/patches/openal-soft-3ds-core1.patch"
readonly LOD3DS_OPENAL_STABILITY_PATCH="${LOD3DS_SOURCE_ROOT}/platform/3ds/patches/openal-soft-3ds-audio-stability.patch"
readonly LOD3DS_OPENAL_CMAKE_PATCH="${LOD3DS_SOURCE_ROOT}/platform/3ds/patches/openal-soft-cmake-empty-deps.patch"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"

lod3ds_require_program() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'Required program not found: %s\n' "$1" >&2
    exit 1
  fi
}

lod3ds_apply_checked_patch() {
  local patch_file="$1"
  local label="$2"

  if git -C "${LOD3DS_OPENAL_CHECKOUT}" apply --reverse --check \
      "${patch_file}" >/dev/null 2>&1; then
    return
  fi

  git -C "${LOD3DS_OPENAL_CHECKOUT}" apply --check "${patch_file}"
  git -C "${LOD3DS_OPENAL_CHECKOUT}" apply "${patch_file}"
  if ! git -C "${LOD3DS_OPENAL_CHECKOUT}" apply --reverse --check \
      "${patch_file}" >/dev/null 2>&1; then
    printf 'OpenAL Soft checkout does not match the pinned %s patch.\n' \
      "${label}" >&2
    exit 1
  fi
}

lod3ds_checkout_openal() {
  local current_revision=""
  local checkout_remote=""
  local patch_file=""
  local patch_fingerprint=""
  local patch_state=""
  local recorded_fingerprint=""
  local created=0

  if [[ ! -d "${LOD3DS_OPENAL_CHECKOUT}/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
      "${LOD3DS_OPENAL_URL}" "${LOD3DS_OPENAL_CHECKOUT}"
    created=1
  fi

  checkout_remote="$(git -C "${LOD3DS_OPENAL_CHECKOUT}" remote get-url origin 2>/dev/null || true)"
  if [[ "${checkout_remote}" != "${LOD3DS_OPENAL_URL}" ]]; then
    printf 'Unexpected OpenAL Soft origin in %s\nExpected: %s\nActual:   %s\n' \
      "${LOD3DS_OPENAL_CHECKOUT}" "${LOD3DS_OPENAL_URL}" "${checkout_remote}" >&2
    exit 1
  fi

  current_revision="$(git -C "${LOD3DS_OPENAL_CHECKOUT}" rev-parse HEAD 2>/dev/null || true)"
  if [[ "${created}" == "1" || "${current_revision}" != "${LOD3DS_OPENAL_REV}" ]]; then
    if [[ "${created}" == "0" && -n "$(git -C "${LOD3DS_OPENAL_CHECKOUT}" status --porcelain 2>/dev/null || true)" ]]; then
      printf 'OpenAL Soft checkout has local changes: %s\n' "${LOD3DS_OPENAL_CHECKOUT}" >&2
      printf 'Use a clean generated checkout or set LOD3DS_OPENAL_SOURCE_DIR.\n' >&2
      exit 1
    fi
    git -C "${LOD3DS_OPENAL_CHECKOUT}" fetch --depth=1 origin "${LOD3DS_OPENAL_REV}"
    git -C "${LOD3DS_OPENAL_CHECKOUT}" checkout --detach --force "${LOD3DS_OPENAL_REV}"
  fi

  current_revision="$(git -C "${LOD3DS_OPENAL_CHECKOUT}" rev-parse HEAD)"
  if [[ "${current_revision}" != "${LOD3DS_OPENAL_REV}" ]]; then
    printf 'OpenAL Soft revision verification failed.\n' >&2
    exit 1
  fi

  # fetch-dependencies.sh may already have applied the complete ordered stack.
  # Use the same revision+patch fingerprint so a later patch that touches the
  # same source file does not make per-patch reverse checks ambiguous.
  patch_state="$(git -C "${LOD3DS_OPENAL_CHECKOUT}" rev-parse --absolute-git-dir)/lod3ds-patch-state"
  patch_fingerprint="$({
    printf '%s\n' "${LOD3DS_OPENAL_REV}"
    for patch_file in \
        "${LOD3DS_OPENAL_CORE_PATCH}" \
        "${LOD3DS_OPENAL_STABILITY_PATCH}" \
        "${LOD3DS_OPENAL_CMAKE_PATCH}"; do
      git hash-object "${patch_file}"
    done
  } | git hash-object --stdin)"
  if [[ -f "${patch_state}" ]]; then
    recorded_fingerprint="$(tr -d '\r\n' < "${patch_state}")"
  fi
  if [[ "${recorded_fingerprint}" == "${patch_fingerprint}" ]]; then
    return
  fi

  if [[ -n "$(git -C "${LOD3DS_OPENAL_CHECKOUT}" status --porcelain 2>/dev/null || true)" ]]; then
    printf 'OpenAL Soft checkout has local changes but no matching patch-state: %s\n' \
      "${LOD3DS_OPENAL_CHECKOUT}" >&2
    printf 'Restore the generated checkout before retrying.\n' >&2
    exit 1
  fi

  lod3ds_apply_checked_patch "${LOD3DS_OPENAL_CORE_PATCH}" \
    'Nintendo 3DS audio'
  lod3ds_apply_checked_patch "${LOD3DS_OPENAL_STABILITY_PATCH}" \
    'Nintendo 3DS audio stability'
  lod3ds_apply_checked_patch "${LOD3DS_OPENAL_CMAKE_PATCH}" \
    'CMake compatibility'
  git -C "${LOD3DS_OPENAL_CHECKOUT}" diff --check
  printf '%s\n' "${patch_fingerprint}" > "${patch_state}"
}

for lod3ds_program in cmake git; do
  lod3ds_require_program "${lod3ds_program}"
done

test -f "${DEVKITPRO}/cmake/3DS.cmake"
test -x "${DEVKITARM}/bin/arm-none-eabi-g++"
mkdir -p "${LOD3DS_BUILD_ROOT}/_deps" "${LOD3DS_OPENAL_PREFIX}"

lod3ds_checkout_openal

cmake -S "${LOD3DS_OPENAL_CHECKOUT}" -B "${LOD3DS_OPENAL_BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_INSTALL_PREFIX="${LOD3DS_OPENAL_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLATFORM=3ds \
  -DLIBTYPE=STATIC \
  -DALSOFT_DLOPEN=OFF \
  -DALSOFT_DLOPEN_NOTES=OFF \
  -DALSOFT_ENABLE_MODULES=OFF \
  -DALSOFT_UTILS=OFF \
  -DALSOFT_NO_CONFIG_UTIL=ON \
  -DALSOFT_EXAMPLES=OFF \
  -DALSOFT_TESTS=OFF \
  -DALSOFT_EAX=OFF \
  -DALSOFT_BACKEND_NDSP=ON \
  -DALSOFT_BACKEND_WAVE=OFF \
  -DALSOFT_EMBED_HRTF_DATA=OFF \
  -DALSOFT_CPUEXT_NEON=OFF \
  -DALSOFT_UPDATE_BUILD_VERSION=OFF \
  -DALSOFT_INSTALL=ON \
  -DALSOFT_INSTALL_CONFIG=OFF \
  -DALSOFT_INSTALL_HRTF_DATA=OFF \
  -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
  -DALSOFT_INSTALL_EXAMPLES=OFF \
  -DALSOFT_INSTALL_UTILS=OFF

cmake --build "${LOD3DS_OPENAL_BUILD_DIR}" --target OpenAL \
  --parallel "${LOD3DS_OPENAL_JOBS}"
cmake --install "${LOD3DS_OPENAL_BUILD_DIR}"

LOD3DS_OPENAL_LIBRARY="${LOD3DS_OPENAL_PREFIX}/lib/libopenal.a"
LOD3DS_OPENAL_INCLUDE_DIR="${LOD3DS_OPENAL_PREFIX}/include/AL"
test -f "${LOD3DS_OPENAL_LIBRARY}"
test -f "${LOD3DS_OPENAL_INCLUDE_DIR}/al.h"

printf 'Pinned OpenAL Soft/NDSP dependency is ready.\n'
printf '  revision: %s\n' "${LOD3DS_OPENAL_REV}"
printf '  library:  %s\n' "${LOD3DS_OPENAL_LIBRARY}"
printf '  includes: %s\n' "${LOD3DS_OPENAL_INCLUDE_DIR}"
