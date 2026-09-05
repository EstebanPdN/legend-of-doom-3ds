#!/usr/bin/env python3
"""Offline contracts for starvation-free music streaming on Nintendo 3DS."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class MusicStreamStabilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (
            ROOT / "src/common/audio/sound/oalsound.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/common/audio/sound/oalsound.cpp"
        ).read_text(encoding="utf-8")

    def test_3ds_stream_worker_does_not_use_lowest_priority_pthread(self):
        create = self.source[
            self.source.index("SoundStream *OpenALSoundRenderer::CreateStream") :
        ]
        create_3ds = create[
            create.index("#ifdef __3DS__") : create.index("#else")
        ]

        self.assertIn("threadCreate(BackgroundProc3DS", create_3ds)
        self.assertIn("svcGetThreadPriority", create_3ds)
        self.assertIn("priority, 1, false", create_3ds)
        self.assertNotIn("StreamThread = std::thread", create_3ds)
        self.assertIn("Thread StreamThread = nullptr;", self.header)

    def test_3ds_music_queue_has_several_seconds_of_headroom(self):
        stream_class = self.source[
            self.source.index("class OpenALSoundStream") :
            self.source.index("bool SetupSource()")
        ]
        self.assertIn("static const int BufferCount = 8;", stream_class)

    def test_3ds_refill_poll_is_shorter_than_upstream_desktop_interval(self):
        background = self.source[
            self.source.index("void OpenALSoundRenderer::BackgroundProc()") :
            self.source.index("void OpenALSoundRenderer::AddStream")
        ]
        self.assertIn("std::chrono::milliseconds(20)", background)
        self.assertIn("std::chrono::milliseconds(100)", background)

    def test_manifest_records_music_stream_policy(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn(
            "audio_music_stream=core1-priority-inherited-20ms-refill-8-pcm-buffers",
            build,
        )

    def test_music_gets_three_x_gain_without_boosting_sfx_listener(self):
        self.assertIn("PlatformOutputGain = 1.0f", self.source)
        self.assertIn("PlatformMusicGain = 3.0f", self.source)
        self.assertIn("AL_MAX_GAIN, PlatformMusicGain", self.source)
        self.assertIn("Renderer->MusicVolume*Volume*PlatformMusicGain", self.source)

        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn(
            "audio_gain=sfx-listener-neutral-1x-music-only-3x-plus-200-percent",
            build,
        )


if __name__ == "__main__":
    unittest.main()
