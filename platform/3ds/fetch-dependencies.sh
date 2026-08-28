#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${LOD3DS_BUILD_ROOT:-${ROOT}/build-3ds}"
DEPS_ROOT="${BUILD_ROOT}/_deps"
DOWNLOADS="${DEPS_ROOT}/downloads"

# shellcheck source=dependencies.sh
source "${ROOT}/platform/3ds/dependencies.sh"

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
      printf 'SHA-256 mismatch for %s\nExpected: %s\nActual:   %s\n' \
        "${url}" "${expected}" "${actual}" >&2
      exit 1
    fi
    mv "${output}.part" "${output}"
  fi
}

checkout_pinned() {
  local url="$1"
  local revision="$2"
  local destination="$3"
  local patch_file=""
  local patch_state=""
  local patch_fingerprint=""
  local recorded_fingerprint=""
  local current=""
  local created=0

  if [[ ! -d "${destination}/.git" ]]; then
    git clone --filter=blob:none --no-checkout "${url}" "${destination}"
    created=1
  fi

  current="$(git -C "${destination}" rev-parse HEAD 2>/dev/null || true)"
  patch_state="$(git -C "${destination}" rev-parse --absolute-git-dir)/lod3ds-patch-state"
  patch_fingerprint="$({
    printf '%s\n' "${revision}"
    for patch_file in "${@:4}"; do
      git hash-object "${patch_file}"
    done
  } | git hash-object --stdin)"
  if [[ -f "${patch_state}" ]]; then
    recorded_fingerprint="$(tr -d '\r\n' < "${patch_state}")"
  fi

  # Later patches may deliberately alter context introduced by earlier ones,
  # so `git apply --reverse --check` on each patch in isolation is not a valid
  # test for an already-applied stack. Record one fingerprint for the pinned
  # revision plus the complete ordered patch set instead.
  if [[ "${current}" == "${revision}" &&
        "${recorded_fingerprint}" == "${patch_fingerprint}" ]]; then
    return
  fi

  if [[ "${created}" == "1" || "${current}" != "${revision}" ]]; then
    if [[ "${created}" == "0" && -n "$(git -C "${destination}" status --porcelain 2>/dev/null || true)" ]]; then
      printf 'Dependency checkout has local changes: %s\n' "${destination}" >&2
      printf 'Remove that generated checkout or restore it before retrying.\n' >&2
      exit 1
    fi
    git -C "${destination}" fetch --depth=1 origin "${revision}"
    git -C "${destination}" checkout --detach --force "${revision}"
  fi

  if [[ -n "$(git -C "${destination}" status --porcelain 2>/dev/null || true)" ]]; then
    printf 'Dependency checkout has local changes but no matching patch-state: %s\n' "${destination}" >&2
    printf 'Remove that generated checkout or restore it before retrying.\n' >&2
    exit 1
  fi

  for patch_file in "${@:4}"; do
    if ! git -C "${destination}" apply --check "${patch_file}"; then
      printf 'Patch does not apply cleanly: %s\n' "${patch_file}" >&2
      exit 1
    fi
    git -C "${destination}" apply "${patch_file}"
  done
  printf '%s\n' "${patch_fingerprint}" > "${patch_state}"
}

for program in git curl unzip awk; do
  require_program "${program}"
done
mkdir -p "${DEPS_ROOT}" "${DOWNLOADS}"

checkout_pinned \
  "${LOD3DS_SDL2_URL}" "${LOD3DS_SDL2_REV}" \
  "${DEPS_ROOT}/SDL2" "${ROOT}/platform/3ds/patches/sdl2-3ds-clean-exit.patch"
checkout_pinned \
  "${LOD3DS_ZMUSIC_URL}" "${LOD3DS_ZMUSIC_REV}" \
  "${DEPS_ROOT}/ZMusic" \
  "${ROOT}/platform/3ds/patches/zmusic-3ds.patch" \
  "${ROOT}/platform/3ds/patches/zmusic-minimp3.patch" \
  "${ROOT}/platform/3ds/patches/zmusic-optional-mpg123.patch"
checkout_pinned \
  "${LOD3DS_MINIMP3_URL}" "${LOD3DS_MINIMP3_REV}" \
  "${DEPS_ROOT}/minimp3"
checkout_pinned \
  "${LOD3DS_NOVAGL_URL}" "${LOD3DS_NOVAGL_REV}" \
  "${DEPS_ROOT}/NovaGL" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-3ds.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-nearclip.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-program-uniforms.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-geometry-probe.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-state-probe.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-state-dedup.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-indexed-quads.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-eyeclip.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-vram-upload.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-performance.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-fixed-color-cache.patch" \
  "${ROOT}/platform/3ds/patches/novagl-gzdoom-linear-sampled-textures.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-stage-log.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-frame-sync.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-watchdog.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-safe-earlyz.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-conservative-submit.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-frustum-guard.patch" \
  "${ROOT}/platform/3ds/patches/novagl-hardware-frame-watchdog.patch"
checkout_pinned \
  "${LOD3DS_MOD_URL}" "${LOD3DS_MOD_REV}" \
  "${DEPS_ROOT}/legend-of-doom" "${ROOT}/platform/3ds/patches/legend-of-doom-3ds.patch"

FREEDOOM_ARCHIVE="${DOWNLOADS}/freedoom-${LOD3DS_FREEDOOM_VERSION}.zip"
FREEDOOM_ROOT="${DEPS_ROOT}/freedoom-${LOD3DS_FREEDOOM_VERSION}"
download_checked "${LOD3DS_FREEDOOM_URL}" "${LOD3DS_FREEDOOM_SHA256}" "${FREEDOOM_ARCHIVE}"
mkdir -p "${FREEDOOM_ROOT}"
unzip -q -o "${FREEDOOM_ARCHIVE}" -d "${FREEDOOM_ROOT}"
test -f "${FREEDOOM_ROOT}/freedoom-${LOD3DS_FREEDOOM_VERSION}/freedoom2.wad"

printf 'Pinned dependencies are ready under:\n  %s\n' "${DEPS_ROOT}"
