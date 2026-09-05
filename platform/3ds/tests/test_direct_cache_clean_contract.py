#!/usr/bin/env python3
"""Offline contracts for PR-26-style direct CPU cache cleaning."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "platform/3ds/patches/novagl-direct-cache-clean.patch"


class DirectCacheCleanContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text(encoding="utf-8")

    def test_experimental_novagl_direct_svc_patch_is_not_distributed(self):
        for script_name in ("fetch-dependencies.sh", "test-patches.sh"):
            script = (ROOT / "platform/3ds" / script_name).read_text(
                encoding="utf-8"
            )
            self.assertNotIn("novagl-direct-cache-clean.patch", script)

        # Keep the experiment as a reviewable historical patch, but never put
        # it in a public dependency stack until physical GPU work resumes.
        self.assertIn("#define GSPGPU_FlushDataCache", self.patch)
        self.assertIn("svcStoreProcessDataCache", self.patch)
        self.assertIn("(GSPGPU_FlushDataCache)(address, size)", self.patch)

    def test_stable_app_and_openal_restore_service_cache_maintenance(self):
        cache = (
            ROOT / "src/common/platform/3ds/cache_3ds.cpp"
        ).read_text(encoding="utf-8")
        openal_build = (
            ROOT / "platform/3ds/build-openal-soft.sh"
        ).read_text(encoding="utf-8")
        rsf = (
            ROOT / "platform/3ds/cia/legend-of-doom-3ds.rsf"
        ).read_text(encoding="utf-8")

        self.assertIn("svcStoreProcessDataCache", cache)
        self.assertIn("GSPGPU_FlushDataCache", cache)
        stable = cache[cache.index("#ifdef LOD3DS_SAFE_SOFTWARE") :]
        self.assertLess(
            stable.index("GSPGPU_FlushDataCache"),
            stable.index("svcStoreProcessDataCache"),
        )
        self.assertNotIn("openal-soft-3ds-fast-cache-flush.patch", openal_build)
        self.assertIn("StoreProcessDataCache: 83", rsf)


if __name__ == "__main__":
    unittest.main()
