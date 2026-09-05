#!/usr/bin/env python3
"""Contracts for the v0.26 base through the v0.30 interface polish."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]


class V026InterfaceContractTests(unittest.TestCase):
    def test_v014_overlay_layout_and_controls_are_restored(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        for label in (
            '"PERFORMANCE"',
            '"MEMORY"',
            '"STATUS"',
            '"QUICK DUMP EXCLUDES 136 MB MEMORY IMAGE"',
            '"L+R+A"',
            '"L+R+X"',
            '"L+R+Y"',
        ):
            self.assertIn(label, diagnostics)
        self.assertNotIn('"DEVELOPER OVERLAY"', diagnostics)
        self.assertNotIn('{ 217, 94, "CLOSE" }', diagnostics)

    def test_fps_and_aim_marker_stay_on_the_upper_screen(self):
        main = (ROOT / "src/d_main.cpp").read_text()
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        framebuffer = (ROOT / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp").read_text()
        crosshair = ROOT / "platform/3ds/assets/aim-crosshair.png"
        self.assertIn('"FPS %llu  %.1fMS"', main)
        self.assertIn("I_3DSComposeGameplayFrame", framebuffer)
        self.assertIn("ComposeNativeMenuFps", diagnostics)
        self.assertIn("AimCrosshairAlpha", diagnostics)
        self.assertTrue(crosshair.is_file())

    def test_render_selector_drives_the_real_canvas(self):
        scale = (ROOT / "src/common/rendering/r_videoscale.cpp").read_text()
        self.assertIn("return 400u * static_cast<uint32_t>(lod3ds_render_scale) / 10u", scale)
        self.assertIn("return 240u * static_cast<uint32_t>(lod3ds_render_scale) / 10u", scale)
        self.assertIn("vid_scale_customwidth = 400 * self / 10", scale)
        self.assertIn("vid_scale_customheight = 240 * self / 10", scale)

    def test_messages_and_dungeon_map_survive_hud_and_level_changes(self):
        main = (ROOT / "src/d_main.cpp").read_text()
        status = (ROOT / "src/g_statusbar/shared_sbar.cpp").read_text()
        notify = (ROOT / "src/console/c_notifybuffer.cpp").read_text()
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        self.assertIn("StatusBar->DrawMessagesOnly(HUD_None)", main)
        self.assertIn("void DBaseStatusBar::DrawMessagesOnly", status)
        self.assertIn("CR_BLACK, textX + 1, line + 1", notify)
        self.assertIn("trackedMapName.Compare(primaryLevel->MapName)", diagnostics)

    def test_unified_engine_menus_and_bottom_hud_polish(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        self.assertNotIn("if (DrawNativePixelMenu(framebuffer))", diagnostics)
        self.assertIn('Font "NewSmallFont", "White"', patch)
        self.assertIn("DrawBottomRoundedRectOutline", diagnostics)
        self.assertIn("counter.Image->Width * 132u / 100u", diagnostics)
        self.assertIn("OverlayTextSized(framebuffer, textX, 209, label", diagnostics)
        self.assertIn("OverlayRect(framebuffer, 16, 219, 288, 2, OverlayBlue)", diagnostics)
        self.assertIn("A_OverlayOffset(OverlayID(), 51, 170)", patch)

    def test_controller_menu_fixed_layout_and_expanded_map(self):
        controls = (ROOT / "wadsrc/static/engine/3dsbinds.txt").read_text()
        input_source = (ROOT / "src/common/platform/3ds/i_input_3ds.cpp").read_text()
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        for binding in ("rshoulder +attack", "lshoulder +altattack",
                        "dpadleft weapprev", "dpadright weapnext",
                        "ltrigger invprev", "rtrigger invnext", "pad_x +speed"):
            self.assertIn(binding, controls)
        self.assertIn("lod3ds_cstick_sensitivity", input_source)
        self.assertIn("lod3ds_camera_mode", input_source)
        self.assertIn('OPTIONMENU "LegendControllerOptions"', patch)
        self.assertIn('OPTIONMENU "LegendControlsMenu"', patch)
        self.assertIn("DrawBottomAutomap(mapPixels.data(), 0, 0, SourceWidth, SourceHeight, 64.0)", diagnostics)

    def test_v030_controls_alignment_and_touch_inventory(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        option_menu = (ROOT / "wadsrc/static/zscript/engine/ui/menu/optionmenu.zs").read_text()
        option_items = (ROOT / "wadsrc/static/zscript/engine/ui/menu/optionmenuitems.zs").read_text()
        self.assertIn("class LegendSplitOptionMenu : OptionMenu", patch)
        self.assertIn("override bool UseSplitOptionLayout() { return true; }", patch)
        self.assertIn("override bool UseCompactSplitSliders() { return true; }", patch)
        self.assertIn("GetSplitLabelLeft", option_menu)
        self.assertIn("GetSplitValueRight", option_menu)
        self.assertIn("current.GetSplitLabelLeft()", option_items)
        self.assertIn("current.GetSplitValueRight() - width", option_items)
        self.assertIn("class LegendControlsReferenceMenu", patch)
        self.assertIn("DrawCaption(mDesc.mTitle, 0, false) - 10", patch)
        self.assertIn('"INPUT"', patch)
        self.assertIn('"ACTION"', patch)
        self.assertIn('Font "NewSmallFont", "White"', patch)
        self.assertIn("BottomSelectItem(row * 4u + column)", diagnostics)

    def test_v030_save_load_uses_independent_screens_and_native_keyboard(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        manager = (ROOT / "src/common/menu/savegamemanager.cpp").read_text()
        menu = (ROOT / "wadsrc/static/zscript/engine/ui/menu/loadsavemenu.zs").read_text()
        self.assertIn("DrawNativeSaveLoadBottomFrame", diagnostics)
        self.assertIn("ComposeNativeSaveLoadTop", diagnostics)
        bottom = diagnostics[diagnostics.index("bool DrawNativeSaveLoadBottomFrame") :]
        bottom = bottom[: bottom.index("void DrawNativeMenuBottomFrame")]
        self.assertIn("OverlayFrame(framebuffer, SectionLeft, SectionTop", bottom)
        self.assertIn("SectionHeight, 2, OverlayIvory", bottom)
        self.assertIn("NativeMenuCustomSave = true", bottom)
        self.assertNotIn("menuPixels +", bottom)
        self.assertNotIn('"SAVE GAME" : "LOAD GAME"', bottom)
        self.assertIn("OverlayTextSized(framebuffer, SectionLeft + 9", bottom)
        top = diagnostics[diagnostics.index("void ComposeNativeSaveLoadTop") :]
        top = top[: top.index("void ComposeNativeMenuFps")]
        self.assertIn("constexpr int TargetTop = 21", top)
        self.assertIn("const int InfoLeft = targetLeft", top)
        self.assertIn("const int infoWidth = targetWidth", top)
        self.assertNotIn("InfoLeft, InfoTop, infoWidth, infoHeight, OverlayInk", top)
        self.assertIn("InfoLeft, InfoTop, infoWidth, 1, OverlayIvory", top)
        self.assertIn("NativeTopCenteredText", top)
        self.assertIn("NativeTopWrappedText", top)
        self.assertNotIn("NativeTopCenteredFontText", top)
        self.assertIn("swkbdInputText", manager)
        self.assertIn("I_3DSPrepareNativeKeyboardTop", manager)
        self.assertIn("void I_3DSPrepareNativeKeyboardTop()", diagnostics)
        self.assertIn("manager.OpenNativeKeyboard", menu)
        self.assertIn("manager.DoSave(Selected, mSaveName)", menu)

    def test_v030_stable_uppercase_menus_and_crosshair(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        compose = diagnostics[diagnostics.index("void ComposeNativeMenuTop") :]
        compose = compose[: compose.index("EBottomPresentation DesiredBottomPresentation")]
        self.assertNotIn('"LODPAUSE"', compose)
        self.assertIn("const int verticalOffset = (6 * height + 120) / 240", diagnostics)
        self.assertIn("stableListMenu", diagnostics)
        self.assertIn("horizontalOffset = stableListMenu ? 10.0f", diagnostics)
        self.assertIn('Selector "M_SKULL1", -25, -5', patch)
        for label in ('"NEW GAME"', '"OPTIONS"', '"LOAD GAME"',
                      '"SAVE GAME"', '"QUIT GAME"'):
            self.assertIn(label, patch)
        self.assertIn('Slider "CAM SENSITIVITY"', patch)
        self.assertIn('0.25, 2.0, 0.05, -1', patch)


if __name__ == "__main__":
    unittest.main()
