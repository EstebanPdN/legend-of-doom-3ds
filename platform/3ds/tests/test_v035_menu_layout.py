import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class MenuLayoutTests(unittest.TestCase):
    def test_split_columns_and_slider_fit_with_equal_margins(self):
        patch = (ROOT / 'platform/3ds/patches/legend-of-doom-3ds.patch').read_text()
        def position(method):
            percent = int(re.search(method + r'\(\).*?GetWidth\(\) \* (\d+) / 100', patch).group(1))
            return 400 * percent // 100
        left = position('GetSplitLabelLeft')
        slider = position('GetSplitValueLeft')
        right = position('GetSplitValueRight')
        items = (ROOT / 'wadsrc/static/zscript/engine/ui/menu/optionmenuitems.zs').read_text()
        cell = int(re.search(r'mSliderCellWidth = .*?\? (\d+) :', items).group(1))
        self.assertLessEqual(slider + 7 * cell, right)
        self.assertGreaterEqual(slider - (left + 14 * 8), 8)
        scale = 1.28
        target_left = (320 - (315 - 84 + 1) * scale) / 2
        left_margin = target_left + (left - 84) * scale
        right_margin = 320 - (target_left + (right - 84) * scale)
        self.assertGreater(left_margin, 24)
        self.assertGreater(right_margin, 24)
        self.assertLess(abs(left_margin - right_margin), 2)

    def test_text_center_does_not_depend_on_selector_width(self):
        for cursor_left in (105, 110, 117):
            minimum, maximum, text_left, text_right = cursor_left, 220, 165, 220
            scale = 1.28
            width = maximum - minimum + 1
            offset = (minimum + maximum - text_left - text_right) * scale / 2
            target = (320 - width * scale) / 2 + offset
            center = target + ((text_left + text_right + 1) / 2 - minimum) * scale
            self.assertAlmostEqual(center, 160)

    def test_save_picture_uses_alpha_sky_without_affecting_camera_textures(self):
        renderer = (ROOT / 'src/rendering/swrenderer/r_swrenderer.cpp').read_text()
        save = renderer.split('void FSoftwareRenderer::WriteSavePic', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('DCanvas pic(width, height, true)', save)
        self.assertIn('std::memset(pic.GetPixels(), 0,', save)
        self.assertLess(save.index('RenderingSavePicture = true'), save.index('mScene.RenderViewToCanvas'))
        self.assertGreater(save.index('RenderingSavePicture = false'), save.index('mScene.RenderViewToCanvas'))
        self.assertIn('DoWriteSavePic(file, SS_RGB', save)
        sky = (ROOT / 'src/rendering/swrenderer/plane/r_skyplane.cpp').read_text()
        self.assertIn('(viewport->RenderingToCanvas && !viewport->RenderingSavePicture)', sky)
