#!/usr/bin/env python3
"""Offline contracts for NovaGL's explicit 3DS cache/queue synchronization."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "platform/3ds/patches/novagl-explicit-cache-sync.patch"
FALLBACK_PATCH = ROOT / "platform/3ds/patches/novagl-hardware-cache-fallback.patch"


class NovaGLExplicitCacheSyncContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text(encoding="utf-8")
        cls.added = "\n".join(
            line[1:]
            for line in cls.patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )

    def test_cache_fallback_follows_explicit_sync_in_both_patch_stacks(self):
        for script_name in ("fetch-dependencies.sh", "test-patches.sh"):
            script = (ROOT / "platform/3ds" / script_name).read_text(
                encoding="utf-8"
            )
            explicit = script.index("novagl-explicit-cache-sync.patch")
            previous = script.index("novagl-hardware-frame-watchdog.patch")
            fallback = script.index("novagl-hardware-cache-fallback.patch")
            self.assertGreater(explicit, previous)
            self.assertGreater(fallback, explicit)

    def test_physical_hardware_fallback_restores_safe_frame_retirement(self):
        fallback = FALLBACK_PATCH.read_text(encoding="utf-8")
        added = "\n".join(
            line[1:]
            for line in fallback.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )
        self.assertIn("C3D_FrameEnd(0)", added)
        self.assertIn("gxCmdQueueWait(q, (s64)2000000000LL)", added)
        self.assertIn('nova_hw_abort("render-queue-timeout")', added)
        self.assertIn("gxCmdQueueStop(q)", added)
        self.assertIn("gxCmdQueueClear(q)", added)

    def test_global_linear_heap_flush_is_replaced_only_with_full_contract(self):
        self.assertNotIn("C3D_FrameEnd(0)", self.added)
        self.assertIn("C3D_FrameEnd(GX_CMDLIST_FLUSH)", self.added)
        self.assertIn("#define C3D_FrameSplit(flags)", self.patch)
        self.assertIn("flags |= GX_CMDLIST_FLUSH", self.added)
        for helper in (
            "nova_c3d_sync_display_transfer_",
            "nova_c3d_sync_texture_copy_",
            "nova_c3d_sync_memory_fill_",
        ):
            self.assertIn(helper, self.added)

    def test_cpu_gpu_ranges_and_transfer_lifetimes_are_explicit(self):
        for token in (
            "tex_load_image_safe",
            "C3D_TexFlush(tex)",
            "nova_transfer_staging_alloc",
            "nova_transfer_staging_defer",
            "GSPGPU_InvalidateDataCache",
            "nova_render_queue_wait_idle",
            "C3Di_RenderQueueWaitDone",
            "nova_midframe_drain_force",
            "map_offset",
            "map_length",
        ):
            self.assertIn(token, self.added)

        # A conditional p3d-only drain cannot protect CPU reads after a frame
        # modified solely by GX MemoryFill/TextureCopy.
        self.assertIn("nova_midframe_drain_force();", self.added)
        self.assertNotIn("already synced - bare wrap", self.added)

    def test_glfinish_unconditionally_fences_p3d_and_gx_through_force_helper(self):
        finish_hunk = self.patch.split("void glFinish(void) {", 1)[1].split(
            "diff --git", 1
        )[0]
        finish_added = "\n".join(
            line[1:]
            for line in finish_hunk.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )

        self.assertIn("nova_midframe_drain_force();", finish_added)
        self.assertNotIn("if (g.p3d_pending)", finish_added)
        self.assertNotIn("C3D_FrameEnd(", finish_added)
        self.assertNotIn("C3D_FrameBegin(", finish_added)

        force_hunk = self.patch.split(
            "+void nova_midframe_drain_force(void) {", 1
        )[1].split("@@", 1)[0]
        frame_end = force_hunk.index("C3D_FrameEnd(GX_CMDLIST_FLUSH)")
        queue_idle = force_hunk.index("nova_render_queue_wait_idle()")
        frame_begin = force_hunk.index("C3D_FrameBegin(0)")
        cleared = force_hunk.index("g.p3d_pending = 0")

        self.assertLess(frame_end, queue_idle)
        self.assertLess(queue_idle, frame_begin)
        self.assertLess(frame_begin, cleared)


if __name__ == "__main__":
    unittest.main()
