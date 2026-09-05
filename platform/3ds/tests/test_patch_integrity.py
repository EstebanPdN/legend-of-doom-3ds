import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('validate_patches', ROOT / 'platform/3ds/tools/validate-patches.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PatchIntegrityTests(unittest.TestCase):
    def test_rejects_v031_truncated_menu(self):
        patch = (ROOT / 'platform/3ds/patches/legend-of-doom-3ds.patch').read_text()
        broken = patch.replace('@@ -0,0 +1,55 @@', '@@ -0,0 +1,54 @@')
        self.assertNotEqual(patch, broken)
        self.assertTrue(MODULE.validate(broken))
        self.assertEqual(MODULE.validate(patch), [])

    def test_new_menu_survives_actual_git_apply(self):
        patch = (ROOT / 'platform/3ds/patches/legend-of-doom-3ds.patch').read_text()
        menu = patch.split('diff --git a/actors/LegendPauseMenu.zs', 1)[1].split('diff --git ', 1)[0]
        menu = 'diff --git a/actors/LegendPauseMenu.zs' + menu
        expected = ''.join(line[1:] + '\n' for line in menu.splitlines()
                           if line.startswith('+') and not line.startswith('+++'))
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run(['git', 'init', '-q', temporary], check=True)
            subprocess.run(['git', '-C', temporary, 'apply', '-'], input=menu, text=True, check=True)
            result = (Path(temporary) / 'actors/LegendPauseMenu.zs').read_text()
        self.assertEqual(result, expected)
        self.assertTrue(result.endswith('    }\n}\n'))

    def test_all_patch_counts_are_consistent(self):
        for path in (ROOT / 'platform/3ds/patches').glob('*.patch'):
            with self.subTest(patch=path.name):
                self.assertEqual(MODULE.validate(path.read_text()), [])

    def test_rejects_missing_and_extra_hunk_lines(self):
        self.assertTrue(MODULE.validate('@@ -0,0 +1,2 @@\n+one\n'))
        self.assertTrue(MODULE.validate('@@ -0,0 +1,1 @@\n+one\n+two\n'))
        self.assertEqual(MODULE.validate('@@ -1,2 +1,2 @@\n same\n\n'), [])
