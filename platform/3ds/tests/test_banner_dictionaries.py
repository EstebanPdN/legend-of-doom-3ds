import importlib.util
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('banner', ROOT / 'platform/3ds/tools/validate-banner.py')
BANNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BANNER)


def fixture(names, nodes):
    offset = 28
    size = 12 + 16 * len(names)
    data = bytearray(offset + size + 4 * (len(names) - 1))
    data[:4] = b'CGFX'
    struct.pack_into('<HH', data, 4, 0xfeff, 20)
    data[20:24] = b'DATA'
    struct.pack_into('<4sII', data, offset, b'DICT', size, len(names) - 1)
    for index, name in enumerate(names):
        node = offset + 12 + 16 * index
        struct.pack_into('<IHH', data, node, *nodes[index])
        if index:
            struct.pack_into('<ii', data, node + 8, len(data) - node - 8,
                             offset + size + 4 * (index - 1) - node - 12)
            data += name + b'\0'
    struct.pack_into('<I', data, 12, len(data))
    struct.pack_into('<I', data, 24, len(data) - 20)
    return bytes(data)


class BannerDictionaryTests(unittest.TestCase):
    def test_unicode_material_regression(self):
        names = [b''] + [name.encode() for name in (
            'Logo • atlas difuso único',
            'Frentes • textura original sin relieve pintado',
            'Contorno • azul morado oro')]
        # Tree metadata from the crashing v0.35 material/LUT dictionaries.
        broken = [(0xffffffff, 2, 0), (198, 0, 1), (366, 3, 2), (206, 1, 3)]
        original = fixture(names, broken)
        with self.assertRaises(BANNER.BannerError):
            BANNER.validate(original)
        repaired = BANNER.repair(original)
        self.assertEqual(BANNER.validate(repaired), (1, 3))
        allowed = {28 + 12 + 16 * i + byte for i in range(4) for byte in range(8)}
        self.assertEqual(len(original), len(repaired))
        self.assertTrue(all(i in allowed for i, (a, b) in enumerate(zip(original, repaired)) if a != b))
        self.assertEqual(BANNER.repair(repaired), repaired)

    def test_byte_lookup_matches_every_key(self):
        names = [b''] + [f'Material {i} • áé東京'.encode() for i in range(100)]
        nodes = BANNER.build_tree(names)
        for i, key in enumerate(names[1:], 1):
            self.assertEqual(BANNER.lookup(nodes, key), i)
        self.assertEqual(BANNER.validate(fixture(names, nodes)), (1, 100))

    def test_valid_ascii_banner_is_unchanged(self):
        names = [b'', b'common', b'common2', b'Front', b'Rear']
        data = fixture(names, BANNER.build_tree(names))
        self.assertEqual(BANNER.repair(data), data)

    def test_packaged_banner_has_valid_lookups(self):
        tables, keys = BANNER.validate((ROOT / 'platform/3ds/assets/banner.cgfx').read_bytes())
        self.assertGreater(tables, 0)
        self.assertGreater(keys, 0)

    def test_size_and_child_corruption_are_rejected(self):
        names = [b'', b'Logo']
        data = bytearray(fixture(names, BANNER.build_tree(names)))
        struct.pack_into('<H', data, 28 + 12 + 4, 2)
        with self.assertRaises(BANNER.BannerError):
            BANNER.validate(data)
        data = bytearray(fixture(names, BANNER.build_tree(names)))
        struct.pack_into('<I', data, 12, len(data) - 1)
        with self.assertRaises(BANNER.BannerError):
            BANNER.validate(data)
