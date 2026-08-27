#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=dependencies.sh
source "${ROOT}/platform/3ds/dependencies.sh"

TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/lod3ds-patch-test.XXXXXX")"
trap 'rm -rf -- "${TEMP_ROOT}"' EXIT

require_program() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'Required program not found: %s\n' "$1" >&2
    exit 1
  fi
}

verify_patch_stack() {
  local name="$1"
  local url="$2"
  local revision="$3"
  shift 3
  local checkout="${TEMP_ROOT}/${name}"
  local patch_file

  git init -q "${checkout}"
  git -C "${checkout}" remote add origin "${url}"
  git -C "${checkout}" fetch -q --depth=1 origin "${revision}"
  git -C "${checkout}" checkout -q --detach FETCH_HEAD
  test "$(git -C "${checkout}" rev-parse HEAD)" = "${revision}"

  for patch_file in "$@"; do
    git -C "${checkout}" apply --check "${patch_file}"
    git -C "${checkout}" apply "${patch_file}"
  done
  git -C "${checkout}" diff --check
  printf 'patch-stack=%s revision=%s patches=%u status=ok\n' \
    "${name}" "${revision}" "$#"
}

require_program git

verify_patch_stack sdl2 "${LOD3DS_SDL2_URL}" "${LOD3DS_SDL2_REV}" \
  "${ROOT}/platform/3ds/patches/sdl2-3ds-clean-exit.patch"
verify_patch_stack zmusic "${LOD3DS_ZMUSIC_URL}" "${LOD3DS_ZMUSIC_REV}" \
  "${ROOT}/platform/3ds/patches/zmusic-3ds.patch" \
  "${ROOT}/platform/3ds/patches/zmusic-minimp3.patch"
verify_patch_stack novagl "${LOD3DS_NOVAGL_URL}" "${LOD3DS_NOVAGL_REV}" \
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
  "${ROOT}/platform/3ds/patches/novagl-hardware-conservative-submit.patch"
verify_patch_stack openal-soft-3ds "${LOD3DS_OPENAL_URL}" "${LOD3DS_OPENAL_REV}" \
  "${ROOT}/platform/3ds/patches/openal-soft-3ds-core1.patch"
verify_patch_stack legend-of-doom "${LOD3DS_MOD_URL}" "${LOD3DS_MOD_REV}" \
  "${ROOT}/platform/3ds/patches/legend-of-doom-3ds.patch"

printf 'All pinned patch stacks apply cleanly.\n'
