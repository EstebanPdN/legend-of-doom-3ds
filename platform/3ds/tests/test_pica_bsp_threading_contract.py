#!/usr/bin/env python3
"""Offline contracts for safe BSP threading on Nintendo 3DS."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class PicaBSPThreadingContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bsp = (
            ROOT / "src/rendering/hwrenderer/scene/hw_bsp.cpp"
        ).read_text(encoding="utf-8")
        cls.main = (
            ROOT / "src/common/platform/posix/sdl/i_main.cpp"
        ).read_text(encoding="utf-8")

    def test_3ds_compiles_out_the_desktop_job_pool(self):
        queue_region = self.bsp[
            self.bsp.index("thread_local bool isWorkerThread") :
            self.bsp.index("EXTERN_CVAR(Bool, gl_render_segs)")
        ]

        self.assertIn("#ifndef __3DS__\nctpl::thread_pool renderPool(1);", queue_region)
        self.assertIn("#ifndef __3DS__\nstruct RenderJob", queue_region)
        self.assertIn("RenderJob pool[300000]", queue_region)
        self.assertNotIn("RenderJob pool[50000]", queue_region)
        self.assertNotIn("FNativeBSPWorker", self.bsp)
        self.assertNotIn("threadCreate(", self.bsp)
        self.assertNotIn("LightEvent_", self.bsp)

    def test_3ds_render_bsp_is_forced_synchronous(self):
        render = self.bsp[self.bsp.index("void HWDrawInfo::RenderBSP(") :]
        handheld = render[render.index("#ifdef __3DS__") : render.index("#else")]

        self.assertIn("multithread = false", handheld)
        self.assertIn("RenderBSPNode(node)", handheld)
        self.assertIn("Bsp.Unclock()", handheld)
        self.assertNotIn("jobQueue", handheld)
        self.assertNotIn("WorkerThread", handheld)

    def test_every_3ds_profile_overrides_archived_multithread_setting(self):
        setting = self.main[self.main.index('Args->AppendArg("+gl_multithread")') :]
        setting = setting[: setting.index('Args->AppendArg("+gl_lights")')]

        self.assertEqual(setting.count('Args->AppendArg("0")'), 1)
        self.assertNotIn('Args->AppendArg("1")', setting)
        self.assertNotIn("LOD3DS_SAFE_SOFTWARE", setting)

    def test_desktop_keeps_the_existing_ctpl_path(self):
        self.assertIn("#ifndef __3DS__\n#include \"ctpl.h\"", self.bsp)
        self.assertIn("ctpl::thread_pool renderPool(1)", self.bsp)
        self.assertIn("auto future = renderPool.push", self.bsp)
        self.assertIn("future.wait()", self.bsp)


if __name__ == "__main__":
    unittest.main()
