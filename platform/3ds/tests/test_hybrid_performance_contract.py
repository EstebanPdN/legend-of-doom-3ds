#!/usr/bin/env python3
"""Offline contracts for the bounded New 3DS hybrid renderer."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class HybridPerformanceContractTests(unittest.TestCase):
    def test_build_profile_enables_safe_world_and_hybrid_presenter(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        profile = build[build.index("  hardware-hybrid)") :]
        profile = profile[: profile.index("    ;;")]
        self.assertIn("SAFE_SOFTWARE=ON", profile)
        self.assertIn("HYBRID_PERFORMANCE=ON", profile)
        self.assertIn("SDL_AUDIO=OFF", profile)
        self.assertIn("GAME_NO_OPENAL=OFF", profile)
        self.assertIn('-DLOD3DS_HYBRID_PERFORMANCE="${HYBRID_PERFORMANCE}"', build)
        self.assertIn('BUILD_PROFILE="$1"', build)

    def test_launch_uses_reduced_canvas_and_core2_drawer(self):
        main = (ROOT / "src/common/platform/posix/sdl/i_main.cpp").read_text(
            encoding="utf-8"
        )
        drawer = (ROOT / "src/common/rendering/r_thread.cpp").read_text(
            encoding="utf-8"
        )
        scale = (ROOT / "src/common/rendering/r_videoscale.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("LOD3DS_HYBRID_PERFORMANCE", main)
        self.assertIn('Args->AppendArg("320")', main)
        self.assertIn('Args->AppendArg("192")', main)
        self.assertIn('Args->AppendArg("2")', main)
        self.assertIn('Args->AppendArg("+r_scene_multithreaded")', main)
        self.assertIn("threadCreate(WorkerMain3DS", drawer)
        self.assertIn("(i == 0) ? 0 : 2", drawer)
        self.assertIn("PlatformMinimumWidth = 40", scale)
        self.assertIn("PlatformMinimumHeight = 24", scale)
        self.assertIn("CUSTOM_CVAR(Int, lod3ds_render_scale, 8", scale)
        self.assertIn("self != 5 && self != 8 && self != 10", scale)

    def test_presenter_is_one_bounded_texture_not_novagl_world(self):
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("C3D_TexInitVRAM(&hybridTexture", video)
        self.assertIn("C3D_SyncDisplayTransfer(hybridUpload", video)
        self.assertIn("C2D_DrawImage(image, &params", video)
        self.assertIn("HybridMaximumCanvasWidth = 400", video)
        self.assertIn("GPU_LINEAR, GPU_LINEAR", video)
        self.assertLess(
            video.index(
                "ConfigureHybridBgraTexture();",
                video.index("bool I_PolyPresentDirect3DS("),
            ),
            video.index(
                "C2D_DrawImage(image, &params",
                video.index("bool I_PolyPresentDirect3DS("),
            ),
        )
        configure = video[video.index("void ConfigureHybridBgraTexture()") :]
        configure = configure[: configure.index("void DeinitHybridPresenter()")]
        self.assertLess(
            configure.index("GPU_TEVOP_RGB_SRC_G"),
            configure.index("GPU_TEVOP_RGB_SRC_B"),
        )
        self.assertLess(
            configure.index("GPU_TEVOP_RGB_SRC_B"),
            configure.index("GPU_TEVOP_RGB_SRC_ALPHA"),
        )
        self.assertIn("C3D_FrameEnd(GX_CMDLIST_FLUSH)", video)
        self.assertIn("hybridC2DFlushSize", video)
        self.assertIn("hybrid-presenter-sdl-fallback", video)
        self.assertIn("defined(LOD3DS_HYBRID_PERFORMANCE)", framebuffer)

    def test_manifest_and_overlay_identify_the_real_hybrid_path(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("softpoly-core0-core2-pica200-presenter", build)
        self.assertIn("2-explicit-libctru-core0-core2", build)
        self.assertIn(
            "200x120-320x192-400x240-touch-selectable-gameplay-plus-400x240-native-menus",
            build,
        )
        self.assertIn("pica200-bilinear", build)
        self.assertIn("bgra-to-rgb-tev-g-b-alpha-before-draw", build)
        self.assertIn(
            "gsp-active-upload-rows-plus-bounded-citro2d-vertex-range", build
        )
        self.assertIn("I_3DSGameplayResolutionTenths() * 10", diagnostics)
        self.assertIn('"QUICK DUMP"', diagnostics)
        self.assertIn('"FULL DUMP"', diagnostics)
        self.assertIn('"CLEAN DUMPS"', diagnostics)
        self.assertIn("I_3DSRequestDiagnosticDump();", diagnostics)
        self.assertIn("I_3DSRequestFullDiagnosticDump();", diagnostics)
        self.assertIn("I_3DSRequestCleanDiagnosticDumps();", diagnostics)
        self.assertNotIn("FPS LOCK 30", diagnostics)


if __name__ == "__main__":
    unittest.main()
