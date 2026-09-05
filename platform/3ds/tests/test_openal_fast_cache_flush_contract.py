#!/usr/bin/env python3
"""Offline contracts for the dump-driven OpenAL/NDSP stability path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "platform/3ds/patches/openal-soft-3ds-audio-stability.patch"


class OpenALAudioStabilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text(encoding="utf-8")
        cls.added = "\n".join(
            line[1:]
            for line in cls.patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )

    def test_audio_stability_patch_is_in_every_dependency_stack(self):
        for script_name in (
            "fetch-dependencies.sh",
            "test-patches.sh",
            "build-openal-soft.sh",
        ):
            script = (ROOT / "platform/3ds" / script_name).read_text(
                encoding="utf-8"
            )
            self.assertIn("openal-soft-3ds-audio-stability.patch", script)

    def test_build_requires_primary_clean_and_compatibility_fallback(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn("DSP_FlushDataCache", build)
        self.assertIn("svcStoreProcessDataCache", build)
        self.assertIn(
            "audio_cache_clean=process-svc-clean-with-dsp-service-fallback", build
        )
        self.assertIn("audio_ndsp_queue=8-wave-buffers", build)

    def test_patch_doubles_queue_and_retains_a_safe_fallback(self):
        self.assertIn("constexpr int NumWaveBuffers{8};", self.added)
        self.assertIn("mDevice->mBufferSize = mDevice->mUpdateSize * 8u;", self.added)
        self.assertIn("svcStoreProcessDataCache", self.added)
        self.assertIn("if(R_FAILED(flush_result))", self.added)
        self.assertIn("DSP_FlushDataCache", self.added)

    def test_audio_keeps_the_physically_verified_v06_core1_policy(self):
        core_patch = (
            ROOT / "platform/3ds/patches/openal-soft-3ds-core1.patch"
        ).read_text(encoding="utf-8")
        self.assertIn("previousCpuLimit >= 30", core_patch)
        self.assertIn("else if(R_SUCCEEDED(APT_SetAppCpuTimeLimit(30)))", core_patch)
        self.assertIn("APT_SetAppCpuTimeLimit(30)", core_patch)
        self.assertIn("mRaisedCpuLimit", core_patch)
        self.assertIn(
            "APT_SetAppCpuTimeLimit(stream->mPreviousCpuLimit)", core_patch
        )


if __name__ == "__main__":
    unittest.main()
