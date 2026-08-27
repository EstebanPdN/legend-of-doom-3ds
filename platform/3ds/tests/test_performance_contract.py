#!/usr/bin/env python3
"""Offline contracts for the New 3DS CPU/render performance path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class PerformanceContractTests(unittest.TestCase):
    def test_bsp_same_core_worker_is_forced_off(self):
        source = (ROOT / "src/rendering/hwrenderer/scene/hw_bsp.cpp").read_text(
            encoding="utf-8"
        )
        handheld = source[source.index("#ifdef __3DS__") :]
        self.assertIn("CVAR(Bool, gl_multithread, false", handheld)
        self.assertIn("multithread = false;", handheld)

    def test_gles_and_novagl_use_matching_synchronized_buffers(self):
        framebuffer = (
            ROOT / "src/common/rendering/gles/gles_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        manifest = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")

        self.assertIn("mPipelineNbr = 1;", framebuffer)
        self.assertIn("novaSetFrameBuffers(1);", video)
        self.assertIn("-DNOVAGL_FRAME_BUFFERS=1", manifest)
        self.assertIn("novagl_frame_slots=1", manifest)
        self.assertIn("gles_world_vbo_pipeline=1", manifest)
        self.assertIn("gpu_completion_boundary=c3d-queue-wait", manifest)
        self.assertIn("first-frame-per-draw-2s-timeout", manifest)

        watchdog = (
            ROOT / "platform/3ds/patches/novagl-hardware-watchdog.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("gpu-diagnostic.log", watchdog)
        self.assertIn("gxCmdQueueWait", watchdog)
        self.assertIn("svcExitProcess", watchdog)
        self.assertIn("HUNG_OR_PENDING", watchdog)

    def test_telemetry_is_buffered_outside_the_frame_loop(self):
        source = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        end = source[source.index("void I_3DSFrameTelemetryEnd()") :]
        end = end[: end.index("void I_3DSWriteFatalLog")]

        self.assertIn("FrameTelemetryBuffer", end)
        self.assertNotIn("std::fopen", end)
        self.assertNotIn("std::fflush", end)
        dump = source[: source.index("void I_3DSFrameTelemetryBegin()")]
        self.assertIn("FlushFrameTelemetry();", dump)

    def test_novagl_fast_path_is_persistent(self):
        patch = (
            ROOT / "platform/3ds/patches/novagl-gzdoom-performance.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("static_sequential_quad_indices", patch)
        self.assertIn("nova_setup_posuv_fixed_color", patch)
        self.assertIn("block_first = first & ~32767", patch)
        self.assertIn("C3D_DrawArrays(prim, first, count)", patch)
        fixed = (
            ROOT / "platform/3ds/patches/novagl-gzdoom-fixed-color-cache.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("s_fixed_color_valid", fixed)
        self.assertIn("nova_setup_fixed_color", fixed)

    def test_sampled_textures_cannot_overflow_gx_upload_queue(self):
        patch = (
            ROOT
            / "platform/3ds/patches/novagl-gzdoom-linear-sampled-textures.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("params.onVram = false;", patch)
        self.assertIn("slot->is_vram = 0;", patch)
        self.assertIn("32-entry GX queue overflow", patch)
        self.assertIn("in-flight", patch)


if __name__ == "__main__":
    unittest.main()
