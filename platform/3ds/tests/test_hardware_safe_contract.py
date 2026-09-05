#!/usr/bin/env python3
"""Offline contracts for the CPU-only physical-console recovery profile."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class HardwareSafeContractTests(unittest.TestCase):
    def test_hardware_safe_is_the_local_and_ci_default(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/build-3ds.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'BUILD_PROFILE="${LOD3DS_BUILD_PROFILE:-hardware-safe}"', build
        )
        self.assertIn(
            "LOD3DS_BUILD_PROFILE=hardware-safe ./platform/3ds/build.sh",
            workflow,
        )
        self.assertIn("grep -qx 'profile=hardware-safe'", workflow)

    def test_current_version_is_embedded_and_visible_on_the_bottom_screen(self):
        version = (ROOT / "platform/3ds/version.txt").read_text(
            encoding="utf-8"
        ).strip()
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        src_cmake = (ROOT / "src/CMakeLists.txt").read_text(encoding="utf-8")
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")

        self.assertEqual(version, "0.30")
        self.assertIn('-DLOD3DS_PORT_VERSION="${VERSION}"', build)
        self.assertIn("printf 'port_version=%s\\n' \"${VERSION}\"", build)
        self.assertIn('set( LOD3DS_PORT_VERSION "dev" CACHE STRING', root_cmake)
        self.assertIn('LOD3DS_PORT_VERSION="${LOD3DS_PORT_VERSION}"', src_cmake)
        self.assertNotIn('"V" LOD3DS_PORT_VERSION, 2', diagnostics)
        self.assertIn('"port_version=%s\\n", LOD3DS_PORT_VERSION', diagnostics)

    def test_sdl_patch_keeps_upstream_cpu_writable_scanout(self):
        patch = (
            ROOT / "platform/3ds/patches/sdl2-3ds-clean-exit.patch"
        ).read_text(encoding="utf-8")

        self.assertNotIn("SDL_n3dsvideo.c", patch)
        self.assertNotIn("gfxInit(", patch)
        self.assertNotIn("SDL_PIXELFORMAT_BGR24", patch)

    def test_profile_selects_cpu_renderer_with_single_audio_backend(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")
        scale = (
            ROOT / "src/common/rendering/r_videoscale.cpp"
        ).read_text(encoding="utf-8")

        profile = build[build.index("  hardware-safe)") :]
        profile = profile[: profile.index("    ;;")]
        self.assertIn("SAFE_SOFTWARE=ON", profile)
        self.assertIn("SAFE_SOFTWARE_SILENT=OFF", profile)
        self.assertIn("GAME_NO_OPENAL=OFF", profile)
        self.assertIn("SDL_AUDIO=OFF", profile)
        self.assertIn("HARDWARE_DIAGNOSTIC=OFF", profile)

        self.assertIn("#ifdef LOD3DS_SAFE_SOFTWARE", main)
        self.assertIn('Args->AppendArg("320")', main)
        self.assertIn('Args->AppendArg("200")', main)
        self.assertIn('Args->AppendArg("+vid_adapter")', main)
        self.assertIn('Args->AppendArg("2")', main)
        self.assertIn('Args->AppendArg("1")', main)
        self.assertIn('Args->AppendArg("+vid_scalemode")', main)
        self.assertIn('Args->AppendArg("5")', main)
        self.assertIn('Args->AppendArg("+vid_scale_customwidth")', main)
        self.assertIn('Args->AppendArg("+vid_scale_customheight")', main)
        self.assertIn('Args->AppendArg("+vid_scale_custompixelaspect")', main)
        self.assertIn('Args->AppendArg("0.96")', main)
        self.assertIn('Args->AppendArg("+vid_scalefactor")', main)
        self.assertIn('Args->AppendArg("+vid_cropaspect")', main)
        safe_minimums = scale[
            scale.index("#if defined(__3DS__) && (defined(LOD3DS_SAFE_SOFTWARE)") :
        ]
        self.assertIn("sysCallbacks.IsSpecialUI()", safe_minimums)
        self.assertIn("min_width = 400;", safe_minimums)
        self.assertIn("min_height = 240;", safe_minimums)

    def test_softpoly_is_reachable_and_sdl_driver_is_forced(self):
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            '#error "LOD3DS_SAFE_SOFTWARE requires the CPU renderer', video
        )
        forced = video[video.index("#ifdef LOD3DS_SAFE_SOFTWARE") :]
        self.assertIn("Priv::softpolyEnabled = true;", forced)
        self.assertIn('SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software")', video)
        softpoly = video[video.index("if (Priv::softpolyEnabled)") :]
        fallback = softpoly.index("fb = new OpenGLESRenderer::OpenGLFrameBuffer")
        self.assertLess(softpoly.index("fb = new PolyFrameBuffer"), fallback)
        self.assertIn(
            "#if defined(__3DS__) && defined(LOD3DS_SAFE_SOFTWARE)", softpoly
        )
        self.assertIn(
            "Hardware-safe build could not initialize the CPU framebuffer", softpoly
        )
        self.assertIn("SDL_WINDOWPOS_CENTERED_DISPLAY(0)", video)

    def test_crt_start_objects_are_profile_specific(self):
        cmake = (ROOT / "src/CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('set( N3DS_CRT_VARIANT "pica" )', cmake)
        self.assertIn('set( N3DS_CRT_VARIANT "safe-software" )', cmake)
        self.assertIn(
            'legend-3ds-crt-${N3DS_CRT_VARIANT}', cmake
        )

    def test_safe_heap_split_favors_the_gzdoom_vm(self):
        crt = (
            ROOT / "src/common/platform/3ds/3dsx_crt0_safe.s"
        ).read_text(encoding="utf-8")
        memory = (ROOT / "src/common/platform/3ds/memory.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn(".word 92*1024*1024", crt)
        self.assertIn(".word 4*1024*1024", crt)
        self.assertIn("u32 __ctru_linear_heap_size = 4 * 1024 * 1024", memory)
        self.assertIn("void __system_allocateHeaps(void)", memory)
        self.assertIn(
            "ConventionalHeapAddressCapacity = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN",
            memory,
        )
        self.assertIn("ConventionalHeapMaximum == 92 * 1024 * 1024", memory)
        self.assertIn("heapBytes = ConventionalHeapMaximum", memory)
        self.assertIn("linearBytes == MinimumLinearHeapBytes", memory)
        self.assertIn("linearBytes - HeapRetryStepBytes", memory)

    def test_cia_heap_policy_covers_new3ds_and_legacy_limits(self):
        page_mask = 0xFFF
        minimum_linear = 4 * 1024 * 1024
        maximum_heap = 92 * 1024 * 1024

        def split(maximum_commit, current_commit):
            remaining = (maximum_commit - current_commit) & ~page_mask
            heap = min(remaining - minimum_linear, maximum_heap)
            linear = min(remaining - heap, 32 * 1024 * 1024)
            return heap, linear

        # Exact values recovered from physical Luma dump 00000109.
        self.assertEqual(
            split(0x07C00000, 0x00AAC000),
            (92 * 1024 * 1024, 0x01554000),
        )
        # Same static image under a 64 MiB legacy resource limit.
        self.assertEqual(
            split(64 * 1024 * 1024, 0x00AAC000),
            (0x03154000, 4 * 1024 * 1024),
        )

    def test_cia_uses_the_physical_console_bootable_fallback(self):
        rsf = (ROOT / "platform/3ds/cia/legend-of-doom-3ds.rsf").read_text(
            encoding="utf-8"
        )
        self.assertIn("SystemMode: 64MB", rsf)
        self.assertIn("SystemModeExt: 124MB", rsf)

    def test_safe_renderer_has_a_contiguous_startup_reserve(self):
        memory = (ROOT / "src/common/platform/3ds/memory.cpp").read_text(
            encoding="utf-8"
        )
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        vertices = (
            ROOT / "src/common/rendering/hwrenderer/data/flatvertices.h"
        ).read_text(encoding="utf-8")

        self.assertIn("RendererMemoryReserveBytes = 2 * 1024 * 1024", memory)
        self.assertIn("I_3DSReserveRendererMemory()", memory)
        self.assertIn("I_3DSReleaseRendererMemory();", framebuffer)
        self.assertLess(
            framebuffer.index("I_3DSReleaseRendererMemory();"),
            framebuffer.index("mVertexData = new FFlatVertexBuffer"),
        )
        self.assertIn("static const unsigned int BUFFER_SIZE = 65536", vertices)

    def test_safe_build_requires_openal_and_rejects_sdl_audio(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn('-DSDL_AUDIO="${SDL_AUDIO}"', build)
        self.assertIn('-DNO_OPENAL="${GAME_NO_OPENAL}"', build)
        self.assertIn("alcOpenDevice ndspInit __system_allocateHeaps", build)
        self.assertIn("N3DSAUDIO_Init", build)

    def test_safe_build_rejects_experimental_gpu_present_and_checks_audio_flush(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        cache = (ROOT / "src/common/platform/3ds/cache_3ds.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "I_PolyPresentDirect3DS nova_init_ex novaSwapBuffers novaSetSwapInterval",
            build,
        )
        self.assertIn("missing the low-jitter cache clean", build)
        self.assertIn("missing the compatibility cache-clean fallback", build)
        self.assertIn(
            "#if defined(__3DS__) && !defined(LOD3DS_SAFE_SOFTWARE)",
            framebuffer,
        )
        self.assertIn(
            'I_FatalError("Hardware-safe build rejected the NovaGL framebuffer path.")',
            video,
        )
        safe_cache = cache[cache.index("#ifdef LOD3DS_SAFE_SOFTWARE") :]
        self.assertLess(
            safe_cache.index("GSPGPU_FlushDataCache"),
            safe_cache.index("svcStoreProcessDataCache"),
        )
        self.assertIn("softpoly-frame-3-present-ready", framebuffer)

    def test_dumps_have_quick_and_full_modes(self):
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.h"
        ).read_text(encoding="utf-8")

        self.assertIn("QuickDumpButtonMask", diagnostics)
        self.assertIn("FullDumpButtonMask", diagnostics)
        self.assertIn("CleanDumpButtonMask", diagnostics)
        self.assertIn("WriteMemorySurvey(manifest, directory)", diagnostics)
        self.assertIn("DumpReadableMemory(manifest, directory)", diagnostics)
        self.assertIn("memory.status=omitted_quick", diagnostics)
        self.assertIn('"%s/memory.bin"', diagnostics)
        self.assertIn("hash=omitted_on_device", diagnostics)
        self.assertIn("IoChunkBytes = 512u * 1024u", diagnostics)
        self.assertNotIn("MemorySegmentBytes", diagnostics)
        self.assertIn("I_3DSRequestFullDiagnosticDump", header)
        self.assertIn("I_3DSRequestCleanDiagnosticDumps", header)
        self.assertIn("I_3DSDiagnosticTouch", header)

    def test_bottom_overlay_and_software_frame_clear_are_present(self):
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        framebuffer = (
            ROOT / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("DrawBottomOverlay", diagnostics)
        self.assertIn('"PERFORMANCE"', diagnostics)
        self.assertIn('"FPS"', diagnostics)
        self.assertIn("DrawBottomGameplay", diagnostics)
        self.assertIn("DrawBottomAutomap", diagnostics)
        self.assertIn("DrawBottomItems", diagnostics)
        self.assertIn("EBottomPresentation::Gameplay", diagnostics)
        self.assertIn("EBottomGameplayTab::Map", diagnostics)
        self.assertIn("EBottomGameplayTab::Items", diagnostics)
        self.assertIn('#include "bottom_game_interface.inc"', diagnostics)
        self.assertIn('"AUDIO READY"', diagnostics)
        self.assertIn('"QUICK DUMP"', diagnostics)
        self.assertIn('"FULL DUMP"', diagnostics)
        self.assertIn('"CLEAN DUMPS"', diagnostics)
        self.assertNotIn('"TAP NEXT 10-100%"', diagnostics)
        self.assertNotIn('"FPS LOCK 30"', diagnostics)
        self.assertIn("DrawDiagnosticProgress", diagnostics)
        self.assertIn('"DO NOT POWER OFF"', diagnostics)
        self.assertIn("DiagnosticProgressValue * 100", diagnostics)
        self.assertIn('"YOUR MEMORY IS FULL"', diagnostics)
        self.assertIn("RemoveDumpTreeContents", diagnostics)
        self.assertIn("FOverlayColor OverlayParchment{ 0, 0, 0 }", diagnostics)
        self.assertIn("FOverlayColor OverlayBlue{ 22, 62, 255 }", diagnostics)
        self.assertIn("framebuffer[offset + 0] = 255", diagnostics)
        self.assertIn("framebuffer[offset + 1] = color.Blue", diagnostics)
        self.assertIn("framebuffer[offset + 3] = color.Red", diagnostics)
        self.assertIn("DumpUiDelayMilliseconds = 1000", diagnostics)
        self.assertIn("LCDs copied to RAM before progress UI", diagnostics)
        dump = diagnostics[diagnostics.index("void WriteDiagnosticDump") :]
        self.assertLess(
            dump.index("CaptureScreenSnapshot(GFX_BOTTOM, bottomSnapshot)"),
            dump.index("DiagnosticProgressActive = true"),
        )
        self.assertLess(
            dump.index("DiagnosticProgressActive = true"),
            dump.index('WriteScreenCapture(manifest, directory, bottomSnapshot, "bottom")'),
        )
        self.assertIn("gspWaitForVBlank();", dump[: dump.index("const FMemorySurvey")])
        self.assertNotIn("notificationVisible", diagnostics)
        self.assertNotIn("OverlayRect(framebuffer, 12, 136, 296, 42", diagnostics)
        begin = framebuffer[framebuffer.index("void PolyFrameBuffer::BeginFrame()") :]
        self.assertIn("std::memset(mCanvas->GetPixels()", begin)
        self.assertIn("CanvasBytesPerPixel", begin)

    def test_gameplay_ui_assets_and_pause_menu_are_packaged(self):
        import struct

        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        patch = (
            ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch"
        ).read_text(encoding="utf-8")
        doommenu = (ROOT / "src/menu/doommenu.cpp").read_text(encoding="utf-8")
        frame = (ROOT / "platform/3ds/assets/bottom-game-frame.png").read_bytes()
        packed = (
            ROOT / "src/common/platform/3ds/bottom_game_interface.inc"
        ).read_text(encoding="utf-8")

        self.assertEqual(struct.unpack(">II", frame[16:24]), (320, 240))
        self.assertIn("BottomGameFrameWidth = 320", packed)
        self.assertIn("BottomFaceNormalData", packed)
        self.assertIn("BottomHeartFullData", packed)
        self.assertIn("BottomItemTriforceData", packed)
        self.assertNotIn('"${MOD_PACKAGE_SOURCE}/graphics/LODPAUSE.png"', build)
        self.assertIn('LISTMENU "LegendPauseMenu"', patch)
        pause = patch[patch.index('LISTMENU "LegendPauseMenu"') :]
        self.assertIn('Font "NewSmallFont", "White"', pause)
        self.assertLess(
            pause.index('Resume "RESUME GAME"'), pause.index('TextItem "NEW GAME"')
        )
        self.assertLess(pause.index('TextItem "NEW GAME"'), pause.index('TextItem "OPTIONS"'))
        self.assertLess(pause.index('TextItem "OPTIONS"'), pause.index('TextItem "LOAD GAME"'))
        self.assertLess(pause.index('TextItem "LOAD GAME"'), pause.index('TextItem "SAVE GAME"'))
        self.assertIn("DimAmount = 0.72", patch)
        self.assertNotIn('Position 600, 600', pause)
        self.assertIn('OPTIONMENU "LegendOptionsMenu"', patch)
        self.assertIn('OPTIONMENU "LegendSoundOptions"', patch)
        self.assertIn('OPTIONMENU "LegendDisplayOptions"', patch)
        self.assertIn('OPTIONMENU "LegendDeveloperOptions"', patch)
        self.assertIn('"snd_mastervolume"', patch)
        self.assertIn('"snd_musicvolume"', patch)
        self.assertIn('"snd_sfxvolume"', patch)
        self.assertIn('"lod3ds_map_collisions"', patch)
        self.assertIn("A_OverlayOffset(OverlayID(), 51, 170)", patch)
        self.assertIn("A_WeaponOffset(35, 32)", patch)
        self.assertNotIn('"OptionsMenu", 0', pause)
        self.assertIn('FName("LegendPauseMenu")', doommenu)
        self.assertIn("gamestate == GS_LEVEL", doommenu)
        self.assertNotIn("amount = 1.0f", doommenu)

    def test_native_menu_art_replaces_title_and_hides_overlay_until_gameplay(self):
        import struct

        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        d_main = (ROOT / "src/d_main.cpp").read_text(encoding="utf-8")
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        top = (ROOT / "platform/3ds/assets/menu-top.png").read_bytes()
        bottom = (ROOT / "platform/3ds/assets/menu-bottom.png").read_bytes()
        story = (ROOT / "platform/3ds/assets/menu-story.png").read_bytes()

        self.assertEqual(struct.unpack(">II", top[16:24]), (400, 240))
        self.assertEqual(struct.unpack(">II", bottom[16:24]), (320, 240))
        self.assertEqual(struct.unpack(">II", story[16:24]), (400, 240))
        self.assertIn('"${MOD_PACKAGE_SOURCE}/graphics/TITLEPIC.png"', build)
        self.assertIn('"${MOD_PACKAGE_SOURCE}/graphics/ZSTORY.png"', build)
        self.assertIn('copy_directory "${MOD_SOURCE}" "${MOD_PACKAGE_SOURCE}"', build)
        self.assertIn('zipdir" -df "${MOD_PK3}" "${MOD_PACKAGE_SOURCE}"', build)
        self.assertIn('#include "menu_bottom_screen.inc"', diagnostics)
        self.assertIn("void DrawMenuBottomScreen", diagnostics)
        self.assertIn("gamestate != GS_DEMOSCREEN", diagnostics)
        self.assertIn("gamestate != GS_LEVEL", diagnostics)
        self.assertIn("MenuStoryPage", diagnostics)
        self.assertIn("EBottomPresentation::MenuDimmed", diagnostics)
        self.assertIn("menuactive == MENU_Off", diagnostics)
        self.assertIn("DesiredBottomPresentation", diagnostics)
        self.assertIn("I_3DSSetMenuStoryPage(creditPage)", d_main)
        self.assertIn("return gamestate != GS_LEVEL", d_main)

    def test_bottom_ui_uses_live_face_zoomed_textured_map_and_touch_pause(self):
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        d_main = (ROOT / "src/d_main.cpp").read_text(encoding="utf-8")
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        main = (ROOT / "src/common/platform/posix/sdl/i_main.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('GetFace(StatusBar->CPlayer, "STF", 5)', diagnostics)
        self.assertIn("BottomMapZoomedOut ? 32.0 : 12.0", diagnostics)
        self.assertIn("BottomFloorTextureColor", diagnostics)
        self.assertIn("playerDetached", diagnostics)
        self.assertIn("DrawBottomCircle", diagnostics)
        self.assertIn('"%03d"', diagnostics)
        self.assertIn("EBottomPresentation::NativeMenu", diagnostics)
        self.assertIn("I_3DSRouteNativeMenuFrame", diagnostics)
        self.assertIn("CurrentMenu->CallResponder(&event)", diagnostics)
        self.assertNotIn("DrawBottomPauseMenu", diagnostics)
        self.assertIn("CaptureNativeMenuBase", framebuffer)
        self.assertIn("CVAR(Bool, lod3ds_top_hud, true", d_main)
        self.assertIn("else if (lod3ds_top_hud)", d_main)
        self.assertIn("NativeMenuRequestedScale = 1.28f", diagnostics)
        self.assertIn("NativeOptionTitleScale = 1.44f", diagnostics)
        self.assertIn("CVAR(Bool, lod3ds_map_collisions, true", diagnostics)
        self.assertIn("int mapWidth = 204, int mapHeight = 156", diagnostics)
        self.assertIn("Draw3DSFpsCounter", d_main)
        self.assertIn("StatusBar->DrawCrosshair();", d_main)
        self.assertIn('Args->AppendArg("+name")', main)
        self.assertIn('Args->AppendArg("Link")', main)
        self.assertIn('Args->AppendArg("+wipetype")', main)

    def test_arm_softpoly_sky_path_is_watertight(self):
        triangles = (
            ROOT / "src/common/rendering/polyrenderer/drawers/poly_thread.cpp"
        ).read_text(encoding="utf-8")
        spans = (
            ROOT / "src/common/rendering/polyrenderer/drawers/screen_triangle.cpp"
        ).read_text(encoding="utf-8")
        sky = (
            ROOT / "src/rendering/swrenderer/plane/r_skyplane.cpp"
        ).read_text(encoding="utf-8")
        scene = (
            ROOT / "src/rendering/swrenderer/scene/r_scene.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("clipd[j] < 0.0f", triangles)
        self.assertNotIn("if (vcount == 128)", triangles)
        self.assertNotIn("homogeneous center", triangles)
        self.assertNotIn("zbufferLine[x] >= w[x] + depthbias && x < xend", spans)
        self.assertIn("FillTransparentSkyBackground", sky)
        self.assertIn("viewport->GetDest(Thread->X1, 0) + 3", sky)
        self.assertIn("if (*alpha == 0)", sky)
        self.assertIn("DrawSkyColumn(Thread->X1 + column, first, skyEnd)", sky)
        self.assertIn("CurrentPortalUniq != 0", sky)
        self.assertIn("FillPrimarySkyBackground", sky)
        self.assertIn("Level->skytexture1", sky)
        self.assertNotIn("pl->sky == 0", sky)
        self.assertNotIn("HasNonPrimarySkyPlanes", sky)
        self.assertIn("RenderSkyPlane(thread).FillPrimarySkyBackground();", scene)
        sky_fill = scene.index("RenderSkyPlane(thread).FillPrimarySkyBackground();")
        self.assertLess(scene.index("thread->PlaneList->Render();"), sky_fill)
        self.assertLess(sky_fill, scene.index("thread->Portal->RenderPlanePortals();"))
        self.assertIn("I_3DSRecordSkyFallback(filledPixels)", sky)
        self.assertIn("RepairIsolatedTopSkyColumns", sky)
        self.assertIn("NeighbourMaxDistance = 16", sky)
        self.assertIn("IntrusionMinDistance = 96", sky)
        self.assertIn("I_3DSRecordSkyColumnRepair", sky)

    def test_dump_pauses_entire_device_and_output_gain_is_neutral(self):
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        music = (
            ROOT / "src/common/audio/music/music.cpp"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "src/common/audio/music/s_music.h"
        ).read_text(encoding="utf-8")
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")
        openal = (
            ROOT / "src/common/audio/sound/oalsound.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("FScopedDiagnosticAudioPause audioPause", diagnostics)
        self.assertIn("S_PauseMusic()", diagnostics)
        self.assertIn("S_ResumeMusic()", diagnostics)
        self.assertIn("SoundRenderer::INACTIVE_Complete", diagnostics)
        self.assertIn("SoundRenderer::INACTIVE_Active", diagnostics)
        self.assertIn("bool S_IsMusicPaused ()", music)
        self.assertIn("bool S_IsMusicPaused ();", header)
        self.assertNotIn('Args->AppendArg("+snd_musicvolume")', main)
        self.assertNotIn('Args->AppendArg("+snd_mastervolume")', main)
        self.assertNotIn('Args->AppendArg("+snd_sfxvolume")', main)
        self.assertIn("PlatformOutputGain = 1.0f", openal)
        self.assertGreaterEqual(
            openal.count("alListenerf(AL_GAIN, PlatformOutputGain)"), 2
        )
        self.assertIn("alListenerf(AL_GAIN, 0.0f)", openal)

    def test_top_lcd_presentation_has_no_side_bars(self):
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        present = video[video.index("void I_PolyPresentUnlock") :]
        self.assertIn('Args->AppendArg("+vid_scale_custompixelaspect")', main)
        self.assertIn('Args->AppendArg("0.96")', main)
        self.assertNotIn("width = ClientWidth;", present)
        self.assertNotIn("height = ClientHeight;", present)

    def test_exit_supervisor_does_not_kill_a_suspended_home_session(self):
        lifecycle = (
            ROOT / "src/common/platform/3ds/lifecycle_3ds.cpp"
        ).read_text(encoding="utf-8")
        main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("aptShouldJumpToHome() && aptIsActive()", lifecycle)
        self.assertIn("svcExitProcess()", lifecycle)
        self.assertIn("~F3DSExitSupervisorScope()", main)
        self.assertIn("I_3DSStopExitSupervisor();", main)

    def test_openal_owns_and_joins_the_libctru_ndsp_worker(self):
        patch = (
            ROOT / "platform/3ds/patches/openal-soft-3ds-core1.patch"
        ).read_text(encoding="utf-8")

        self.assertIn("bool mNdspInitialized{false};", patch)
        self.assertIn("if(!alcNdspInit())", patch)
        self.assertIn("alcNdspExit();", patch)
        self.assertIn("worker alive after the device was destroyed", patch)


if __name__ == "__main__":
    unittest.main()
