#!/usr/bin/env python3
"""Offline contracts for the New 3DS CPU/render performance path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class PerformanceContractTests(unittest.TestCase):
    def test_safe_profile_disables_truecolor_sampler_work(self):
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")

        for cvar in ("r_mipmap", "r_minfilter", "r_magfilter", "r_dynlights"):
            self.assertIn(
                f'Args->AppendArg("+{cvar}");\n\tArgs->AppendArg("0");',
                main,
            )

    def test_new3ds_frame_budget_disables_secondary_work(self):
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")

        expected = {
            "vid_maxfps": "0",
            "gl_sort_textures": "1",
            "r_actorspriteshadow": "0",
            "r_maxparticles": "1024",
            "gl_particles_style": "0",
            "r_portal_recursions": "1",
            "r_mirror_recursions": "1",
        }
        for cvar, value in expected.items():
            self.assertIn(
                f'Args->AppendArg("+{cvar}");\n\tArgs->AppendArg("{value}");',
                main,
            )

        # The direct software scanout fast path must not be defeated by an
        # archived desktop colour transform.
        for cvar, value in {
            "vid_gamma": "1.0",
            "vid_contrast": "1.0",
            "vid_brightness": "0.0",
            "vid_saturation": "1.0",
        }.items():
            self.assertIn(
                f'Args->AppendArg("+{cvar}");\n\tArgs->AppendArg("{value}");',
                main,
            )

    def test_new3ds_render_is_uncapped_without_changing_35hz_game_logic(self):
        video = (ROOT / "src/common/rendering/v_video.cpp").read_text(
            encoding="utf-8"
        )
        limiter = video[video.index("CUSTOM_CVAR(Int, vid_maxfps") :]
        limiter = limiter[: limiter.index("CUSTOM_CVAR(Int, vid_preferbackend")]

        hybrid = limiter[
            limiter.index("LOD3DS_HYBRID_PERFORMANCE") : limiter.index("#else")
        ]
        self.assertIn("if (self != 0) self = 0;", hybrid)
        self.assertNotIn("MinimumRenderFPS", hybrid)
        self.assertIn("const int MinimumRenderFPS = GameTicRate", limiter)

    def test_map01_exterior_keeps_clouds_and_culls_remote_geometry(self):
        sky = (
            ROOT / "src/rendering/swrenderer/plane/r_skyplane.cpp"
        ).read_text(encoding="utf-8")
        opaque = (
            ROOT / "src/rendering/swrenderer/scene/r_opaque_pass.cpp"
        ).read_text(encoding="utf-8")
        portals = (
            ROOT / "src/rendering/swrenderer/scene/r_portal.cpp"
        ).read_text(encoding="utf-8")
        colormaps = (
            ROOT / "src/rendering/swrenderer/r_swcolormaps.cpp"
        ).read_text(encoding="utf-8")
        colormap_header = (
            ROOT / "src/rendering/swrenderer/r_swcolormaps.h"
        ).read_text(encoding="utf-8")
        walls = (
            ROOT / "src/rendering/swrenderer/line/r_wallsetup.cpp"
        ).read_text(encoding="utf-8")
        flats = (
            ROOT / "src/rendering/swrenderer/plane/r_flatplane.cpp"
        ).read_text(encoding="utf-8")
        sprites = (
            ROOT / "src/rendering/swrenderer/scene/r_light.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn('MapName.CompareNoCase("MAP01") == 0', sky)
        self.assertIn("DrawSkyColumn", sky)
        self.assertIn("FillMap01DistanceFogColumn", sky)
        self.assertNotIn("BBoxWithinMap01Distance", opaque)
        self.assertNotIn("SubtreeCanBeDistanceCulled", opaque)
        self.assertIn("RenderBSPNode(bsp->children[side]);", opaque)
        self.assertIn("FarClipLine farclip(Thread);", opaque)
        self.assertIn("LineIsOutsideDistance", opaque)
        self.assertNotIn("I_3DSRecordDrawDistanceBspCull();", opaque)
        self.assertIn("pl->portal->mType == PORTS_SKYVIEWPOINT", portals)
        self.assertIn("I_3DSRecordSkyViewpointPortalSkip();", portals)
        self.assertIn("Map01DistanceFogStart = 1536.0", colormap_header)
        self.assertIn("Map01DistanceFogEnd = 2048.0", colormap_header)
        self.assertIn(
            "amount = amount * amount * (3.0 - 2.0 * amount)", colormaps
        )
        self.assertIn('CheckForTexture("BLACK",', colormaps)
        self.assertIn("!Is3DSMap01IntentionalBlackSector(sector)", colormaps)
        for source in (walls, flats, sprites):
            self.assertIn("Apply3DSMap01DistanceFogVisibility", source)

    def test_native_menu_canvas_and_transparent_death_filter(self):
        scale = (
            ROOT / "src/common/rendering/r_videoscale.cpp"
        ).read_text(encoding="utf-8")
        blend = (
            ROOT / "src/rendering/2d/v_blend.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("sysCallbacks.IsSpecialUI()", scale)
        self.assertIn("min_width = 400;", scale)
        self.assertIn("min_height = 240;", scale)
        self.assertIn("CUSTOM_CVAR(Int, lod3ds_render_scale, 8", scale)
        self.assertIn("return lod3ds_render_scale;", scale)
        self.assertNotIn("I_3DSSetGameplayResolutionTenths", scale)
        self.assertIn("player->playerstate == PST_DEAD", blend)
        self.assertIn("V_AddBlend(0.8f, 0.0f, 0.0f, 0.16f, blend)", blend)

    def test_cross_resolution_wipe_scales_the_full_menu_frame(self):
        texture = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_hwtexture.cpp"
        ).read_text(encoding="utf-8")

        wipe = texture[texture.index("void PolyHardwareTexture::CreateWipeTexture") :]
        wipe = wipe[: wipe.index("void PolyHardwareTexture::CreateImage")]
        self.assertIn("sourceWidth == w && sourceHeight == h", wipe)
        self.assertIn("(2 * x + 1) * sourceWidth * 128", wipe)
        self.assertIn("(2 * y + 1) * sourceHeight * 128", wipe)
        self.assertIn("top * (256 - yfrac)", wipe)

    def test_dump_names_start_with_persistent_three_digit_sequence(self):
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("unsigned HighestExistingDumpSerial()", diagnostics)
        self.assertIn('"%s/%03u-%s-%s"', diagnostics)
        self.assertIn(
            '"directory_format=NNN-mode-YYYYMMDD-HHMMSS\\n"', diagnostics
        )

    def test_nearest_truecolor_paths_guard_lod_math(self):
        flat = (
            ROOT / "src/rendering/swrenderer/plane/r_flatplane.cpp"
        ).read_text(encoding="utf-8")
        wall = (
            ROOT / "src/rendering/swrenderer/drawers/r_draw_rgba.cpp"
        ).read_text(encoding="utf-8")
        sprite = (
            ROOT / "src/rendering/swrenderer/viewport/r_spritedrawer.cpp"
        ).read_text(encoding="utf-8")

        flat_path = flat[flat.index("void RenderFlatPlane::RenderLine") :]
        self.assertIn(
            "!r_mipmap && !r_minfilter && !r_magfilter", flat_path
        )
        nearest = flat_path.index("if (nearestWithoutMipmaps)")
        self.assertLess(nearest, flat_path.index("log2(", nearest))
        self.assertIn("drawerargs.SetTextureLOD(0.0);", flat_path)

        wall_path = wall[wall.index("void SWTruecolorDrawers::DrawWallColumn32") :]
        self.assertIn(
            "!wallargs.mipmapped && !r_minfilter && !r_magfilter",
            wall_path,
        )
        guard = wall_path.index("if (!nearestWithoutMipmaps)")
        self.assertLess(guard, wall_path.index("log2(", guard))
        self.assertIn(
            "filter_nearest = nearestWithoutMipmaps ||", wall_path
        )

        sprite_path = sprite[sprite.index("void SpriteDrawerArgs::DrawMaskedColumn") :]
        self.assertIn("const bool mipmapped = r_mipmap", sprite_path)
        self.assertIn(
            "!mipmapped && !r_minfilter && !r_magfilter", sprite_path
        )
        guard = sprite_path.index("if (!nearestWithoutMipmaps)")
        self.assertLess(guard, sprite_path.index("log2(", guard))
        self.assertIn(
            "filter_nearest = nearestWithoutMipmaps ||", sprite_path
        )

    def test_direct_3ds_presenter_has_exact_layout_and_single_swap_owner(self):
        header = (ROOT / "src/common/rendering/i_video.h").read_text(
            encoding="utf-8"
        )
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        direct = video[video.index("bool I_PolyPresentDirect3DS(") :]
        direct = direct[: direct.index("void I_PolyPresentDeinit()")]

        self.assertIn("bool I_PolyPresentDirect3DS(", header)
        for dimension in (
            "SourceWidth = 320",
            "SourceHeight = 200",
            "ScreenWidth = 400",
            "ScreenHeight = 240",
        ):
            self.assertIn(dimension, direct)
        self.assertIn("x != 0 || y != 0", direct)
        self.assertIn("outputWidth != ScreenWidth", direct)
        self.assertIn("outputHeight != ScreenHeight", direct)
        self.assertIn("physicalWidth != ScreenHeight", direct)
        self.assertIn("physicalHeight != ScreenWidth", direct)

        # libctru exposes the rotated top framebuffer as 240x400. SDL's
        # N3DS offset is 239-y + 240*x, which these two expressions reproduce.
        self.assertIn("framebuffer + outX * ScreenHeight", direct)
        self.assertIn("[ScreenHeight - 1 - outY]", direct)

        # DCanvas/ARGB8888 is numeric AARRGGBB. The SDL-owned GSP_RGBA8
        # framebuffer is numeric RRGGBBAA on this little-endian target.
        self.assertIn("(bgra << 8) | (bgra >> 24)", direct)

        flush = direct.index("I_3DSCleanDataCache")
        swap = direct.index("gfxScreenSwapBuffers(GFX_TOP, false)")
        wait = direct.index("gspWaitForVBlank()")
        self.assertLess(flush, swap)
        self.assertLess(swap, wait)
        self.assertEqual(direct.count("I_3DSCleanDataCache"), 1)
        self.assertEqual(direct.count("gfxScreenSwapBuffers"), 1)
        self.assertEqual(direct.count("gspWaitForVBlank"), 1)

    def test_direct_3ds_presenter_matches_sdl_nearest_sample_centers(self):
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        direct = video[video.index("bool I_PolyPresentDirect3DS(") :]
        direct = direct[: direct.index("void I_PolyPresentDeinit()")]

        # SDL 2.32's nearest scaler uses a truncated 16.16 step and starts at
        # half that step. Preserve that exact pattern rather than left-edge
        # sampling, which selects different source pixels at both axes.
        self.assertNotIn(
            "outX * SourceWidth / ScreenWidth", direct
        )
        self.assertNotIn(
            "outY * SourceHeight / ScreenHeight", direct
        )
        self.assertIn("static_cast<uint32_t>(SourceWidth) << 16", direct)
        self.assertIn("static_cast<uint32_t>(SourceHeight) << 16", direct)
        self.assertIn("SourceXStep / 2", direct)
        self.assertIn("SourceYStep / 2", direct)
        self.assertIn("sourceXPosition >> 16", direct)
        self.assertIn("sourceYPosition >> 16", direct)

        def sdl_nearest_map(source_size, output_size):
            step = (source_size << 16) // output_size
            position = step // 2
            result = []
            for _ in range(output_size):
                result.append(position >> 16)
                position += step
            return result

        for source_size, output_size in ((320, 400), (200, 240)):
            expected = sdl_nearest_map(source_size, output_size)
            self.assertEqual(expected[0], 0)
            self.assertEqual(expected[-1], source_size - 1)
            self.assertNotEqual(
                expected,
                [x * source_size // output_size for x in range(output_size)],
            )

    def test_direct_3ds_presenter_precedes_complete_sdl_fallback(self):
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        update = framebuffer[framebuffer.index("void PolyFrameBuffer::Update()") :]
        update = update[: update.index("void PolyFrameBuffer::UpdatePalette()")]

        direct = update.index("presented = I_PolyPresentDirect3DS")
        fallback = update.index("if (!presented)", direct)
        lock = update.index("I_PolyPresentLock", fallback)
        worker_wait = update.rfind(
            "DrawerThreads::WaitForWorkers();", 0, direct
        )
        self.assertLess(worker_wait, direct)
        self.assertLess(direct, fallback)
        self.assertLess(fallback, lock)

        direct_guard = update.rfind("#if", 0, direct)
        self.assertIn(
            "!defined(LOD3DS_SAFE_SOFTWARE)",
            update[direct_guard:direct],
        )

        neutral_start = update.index("if (neutralColorTransform)", lock)
        neutral = update[neutral_start : update.index("I_PolyPresentUnlock", lock)]
        self.assertGreater(worker_wait, 0)
        self.assertLess(worker_wait, update.index("I_3DSComposeGameplayFrame"))
        self.assertNotIn("DrawerThreads::Execute", update[worker_wait:lock])
        self.assertLess(worker_wait, update.index("std::memcpy(dst, src"))
        self.assertIn("sourcePitchBytes == rowBytes", neutral)
        self.assertIn("for (int row = 0; row < h; ++row)", neutral)

        self.assertIn("copyqueue->Push<MemcpyCommand>", update)
        self.assertIn("copyqueue->Push<CopyAndApplyGammaCommand>", update)
        self.assertIn("DrawerThreads::Execute(copyqueue);", update)

    def test_sky_sentinel_scan_is_row_major_and_slice_local(self):
        sky = (
            ROOT / "src/rendering/swrenderer/plane/r_skyplane.cpp"
        ).read_text(encoding="utf-8")
        fill = sky[sky.index("void RenderSkyPlane::FillTransparentSkyBackground()") :]
        fill = fill[: fill.index("I_3DSRecordDrawDistanceFog(foggedPixels);")]

        self.assertNotIn("!Thread->MainThread", fill)
        self.assertIn("sliceWidth = Thread->X2 - Thread->X1", fill)
        self.assertIn(
            "viewport->GetDest(Thread->X1, 0) + 3", fill
        )
        row_loop = fill.index("for (int y = 0; y < viewheight")
        column_loop = fill.index(
            "for (int column = 0; column < sliceWidth", row_loop
        )
        self.assertLess(row_loop, column_loop)
        self.assertIn("if (*alpha == 0)", fill)
        self.assertIn(
            "DrawSkyColumn(Thread->X1 + column, first, skyEnd)", fill
        )
        self.assertIn(
            "DrawSkyColumn(Thread->X1 + column, first, skyEnd)", fill
        )

    def test_row_major_sky_runs_match_legacy_scan_exhaustively(self):
        width = 4
        height = 3

        def legacy_runs(transparent, x1, x2):
            runs = []
            for x in range(x1, x2):
                y = 0
                while y < height:
                    while y < height and not transparent[y][x]:
                        y += 1
                    first = y
                    while y < height and transparent[y][x]:
                        y += 1
                    if first < y:
                        runs.append((x, first, y))
            return sorted(runs)

        def row_major_runs(transparent, x1, x2):
            starts = [-1] * (x2 - x1)
            runs = []
            for y in range(height):
                for column in range(x2 - x1):
                    is_transparent = transparent[y][x1 + column]
                    if is_transparent:
                        if starts[column] < 0:
                            starts[column] = y
                    elif starts[column] >= 0:
                        runs.append((x1 + column, starts[column], y))
                        starts[column] = -1
            for column, first in enumerate(starts):
                if first >= 0:
                    runs.append((x1 + column, first, height))
            return sorted(runs)

        slices = ((0, width), (0, 2), (2, width), (1, 3))
        for bits in range(1 << (width * height)):
            transparent = [
                [
                    bool(bits & (1 << (y * width + x)))
                    for x in range(width)
                ]
                for y in range(height)
            ]
            for x1, x2 in slices:
                self.assertEqual(
                    legacy_runs(transparent, x1, x2),
                    row_major_runs(transparent, x1, x2),
                )
            self.assertEqual(
                legacy_runs(transparent, 0, width),
                sorted(
                    row_major_runs(transparent, 0, 2)
                    + row_major_runs(transparent, 2, width)
                ),
            )

    def test_bsp_worker_is_compiled_out_and_3ds_path_is_synchronous(self):
        source = (ROOT / "src/rendering/hwrenderer/scene/hw_bsp.cpp").read_text(
            encoding="utf-8"
        )
        handheld = source[source.index("#ifdef __3DS__") :]
        self.assertIn("CVAR(Bool, gl_multithread, false", handheld)
        self.assertNotIn("FNativeBSPWorker", source)
        self.assertNotIn("threadCreate(", source)
        render = source[source.index("void HWDrawInfo::RenderBSP(") :]
        synchronous = render[render.index("#ifdef __3DS__") : render.index("#else")]
        self.assertIn("multithread = false;", synchronous)
        self.assertIn("RenderBSPNode(node);", synchronous)
        self.assertNotIn("jobQueue", synchronous)

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
        self.assertIn("pinned-citro3d-render-queue-wait", manifest)
        self.assertIn("novagl_frame_end=%s", manifest)
        self.assertIn("citro3d-full-linear-fallback", manifest)
        self.assertIn("novagl_cpu_gpu_cache=%s", manifest)
        self.assertIn("explicit-ranges-plus-full-frame-fallback", manifest)
        self.assertIn("first-frame-progress-5s-stall-timeout", manifest)

        watchdog = (
            ROOT / "platform/3ds/patches/novagl-hardware-watchdog.patch"
        ).read_text(encoding="utf-8")
        frame_watchdog = (
            ROOT / "platform/3ds/patches/novagl-hardware-frame-watchdog.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("gpu-diagnostic.log", watchdog)
        self.assertIn("gxCmdQueueWait", watchdog)
        self.assertIn("svcExitProcess", watchdog)
        self.assertIn("HUNG_OR_PENDING", watchdog)
        self.assertIn("submission=bounded-natural-segments", frame_watchdog)
        self.assertIn("-    (C3D_FrameSplit)(0);", frame_watchdog)
        self.assertIn("first-frame-progress-5s-stall-timeout", manifest)

        renderer = (
            ROOT / "platform/3ds/patches/novagl-gzdoom-3ds.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("C3D_DEFAULT_CMDBUF_SIZE * 3u", renderer)
        self.assertIn("NOVA_CMDLIST_SEGMENT_WORDS", renderer)
        self.assertNotIn("gpuCmdBufSize * 3u", renderer)

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
