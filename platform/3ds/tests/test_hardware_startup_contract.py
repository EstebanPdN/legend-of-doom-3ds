#!/usr/bin/env python3
"""Offline contracts for the real-hardware Nintendo 3DS startup path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class HardwareStartupContractTests(unittest.TestCase):
    def test_main_does_not_start_video_or_loading_framebuffer_early(self):
        source = (ROOT / "src/common/platform/posix/sdl/i_main.cpp").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("SDL_InitSubSystem(SDL_INIT_VIDEO)", source)
        self.assertNotIn("I_3DSLoadingScreenStart();", source)

    def test_loading_screen_entry_points_are_log_only_noops(self):
        source = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text(
            encoding="utf-8"
        )
        startup_log = source[source.index("void I_3DSStartupLog(") :]
        startup_log = startup_log[: startup_log.index("void I_3DSLoadingScreenStart()")]
        self.assertNotIn("DrawLoadingScreen(", startup_log)

        start = source[source.index("void I_3DSLoadingScreenStart()") :]
        start = start[: start.index("void I_3DSLoadingScreenFinish()")]
        self.assertNotIn("DrawLoadingScreen(", start)

        finish = source[source.index("void I_3DSLoadingScreenFinish()") :]
        finish = finish[: finish.index("void I_3DSFrameTelemetryBegin()")]
        self.assertNotIn("DrawLoadingScreen(", finish)

    def test_initial_lcd_scanout_is_not_swapped_outside_citro3d(self):
        source = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")

        self.assertNotIn("ClearInitialScanout", source)
        self.assertNotIn("gfxScreenSwapBuffers", source)
        self.assertNotIn("GX_MemoryFill", source)

    def test_manifest_records_hardware_safe_policy(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn(
            "early_loading_screen=disabled-hardware-vram-safety", build
        )
        self.assertIn(
            "initial_scanout=owned-exclusively-by-citro3d", build
        )

    def test_early_z_cannot_submit_an_unbound_first_frame_command_list(self):
        patch = (
            ROOT / "platform/3ds/patches/novagl-hardware-safe-earlyz.patch"
        ).read_text(encoding="utf-8")

        self.assertIn("g.early_z_allowed = 0", patch)
        self.assertIn("(void)enabled", patch)
        self.assertNotIn("+    g.early_z_allowed = enabled ? 1 : 0", patch)

    def test_conservative_hardware_submission_contract(self):
        patch = (
            ROOT
            / "platform/3ds/patches/novagl-hardware-conservative-submit.patch"
        ).read_text(encoding="utf-8")

        self.assertIn("if (g.p3d_pending)", patch)
        self.assertIn("C3D_RenderTargetClear", patch)
        self.assertIn("C3D_FRAME_SYNCDRAW", patch)
        self.assertIn("C3D_RenderTargetCreateFromTex explicitly rejects", patch)
        self.assertIn("cmd_capture[512]", patch)

    def test_3ds_music_uses_only_the_pinned_mp3_decoder(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        patch = (
            ROOT / "platform/3ds/patches/zmusic-optional-mpg123.patch"
        ).read_text(encoding="utf-8")

        self.assertIn("-DUSE_MPG123=OFF", build)
        self.assertIn('option(USE_MPG123 "Enable the libmpg123 decoder" ON)', patch)
        self.assertIn("if(USE_MPG123)", patch)


if __name__ == "__main__":
    unittest.main()
