#!/usr/bin/env python3
"""Offline contracts for the real-hardware Nintendo 3DS startup path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class HardwareStartupContractTests(unittest.TestCase):
    def test_early_animation_starts_only_after_sdl_owns_video(self):
        source = (ROOT / "src/common/platform/posix/sdl/i_main.cpp").read_text(
            encoding="utf-8"
        )
        video = source.index("SDL_InitSubSystem(SDL_INIT_VIDEO)")
        animation = source.index("I_3DSLoadingScreenStart();")
        self.assertLess(video, animation)
        self.assertIn("CPU-writable RGBA8 scanout", source)

    def test_loading_screen_uses_embedded_bounded_animation(self):
        source = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text(
            encoding="utf-8"
        )
        start = source[source.index("void I_3DSLoadingScreenStart()") :]
        start = start[: start.index("void I_3DSLoadingScreenFinish()")]
        finish = source[source.index("void I_3DSLoadingScreenFinish()") :]
        finish = finish[: finish.index("void I_3DSFrameTelemetryBegin()")]
        frames = (
            ROOT / "src/common/platform/3ds/triforce_frames.inc"
        ).read_text(encoding="utf-8")
        self.assertIn("DrawTriforceAnimationFrame(0)", start)
        self.assertIn("threadCreate(TriforceAnimationMain", start)
        self.assertIn("threadJoin(LoadingAnimationThread", finish)
        self.assertIn("TriforceAnimationFrames = 33", frames)
        self.assertIn("TriforceAnimationWidth = 96", frames)

    def test_initial_lcd_scanout_is_not_mutated_during_sdl_video_setup(self):
        source = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")

        self.assertNotIn("ClearInitialScanout", source)
        self.assertNotIn("GX_MemoryFill", source)
        constructor = source[source.index("SDLVideo::SDLVideo ()") :]
        constructor = constructor[: constructor.index("SDLVideo::~SDLVideo ()")]
        self.assertNotIn("gfxScreenSwapBuffers", constructor)

        # The CPU frame presenter is allowed to use the libctru framebuffer
        # already owned by SDL, but it must remain the only explicit swap site.
        direct = source[source.index("bool I_PolyPresentDirect3DS(") :]
        direct = direct[: direct.index("void I_PolyPresentDeinit()")]
        outside_direct = source.replace(direct, "")
        self.assertEqual(direct.count("gfxScreenSwapBuffers"), 1)
        self.assertNotIn("gfxScreenSwapBuffers", outside_direct)

    def test_manifest_records_hardware_safe_policy(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn(
            "early_loading_screen=sdl-owned-rgba8-triforce-animation-96x96-33frames",
            build,
        )
        self.assertIn(
            "printf owned-by-sdl-libctru || printf owned-exclusively-by-citro3d",
            build,
        )

    def test_new3ds_memory_budget_is_explicit_and_bounded(self):
        memory = (ROOT / "src/common/platform/3ds/memory.cpp").read_text(
            encoding="utf-8"
        )
        rsf = (ROOT / "platform/3ds/cia/legend-of-doom-3ds.rsf").read_text(
            encoding="utf-8"
        )
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        vertices = (
            ROOT / "src/common/rendering/hwrenderer/data/flatvertices.h"
        ).read_text(encoding="utf-8")

        self.assertIn("__ctru_linear_heap_size = 32 * 1024 * 1024", memory)
        self.assertIn("void __system_allocateHeaps(void)", memory)
        self.assertIn("ConventionalHeapAddressCapacity == 96 * 1024 * 1024", memory)
        self.assertIn("SystemModeExt: 124MB", rsf)
        self.assertIn("SystemMode: 64MB", rsf)
        self.assertIn("nova_init_ex(NOVA_CMD_BUF_SIZE, 2 * 1024 * 1024", video)
        self.assertIn("static const unsigned int BUFFER_SIZE = 65536", vertices)

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

    def test_physical_candidate_keeps_audio_and_watchdog_separable(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")

        candidate = build[build.index("  hardware-candidate)") :]
        candidate = candidate[: candidate.index("    ;;")]
        self.assertIn("HARDWARE_DIAGNOSTIC=ON", candidate)
        self.assertIn("HARDWARE_DIAGNOSTIC_SILENT=OFF", candidate)
        self.assertIn("#ifdef LOD3DS_HARDWARE_DIAGNOSTIC_SILENT", main)
        self.assertIn('Args->AppendArg("-nosound")', main)

    def test_telemetry_embeds_the_actual_build_profile(self):
        source = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("LOD3DS_BUILD_PROFILE_NAME", source)
        self.assertNotIn('"%s,hardware-diagnostic,New Nintendo 3DS', source)


if __name__ == "__main__":
    unittest.main()
