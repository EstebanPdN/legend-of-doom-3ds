import csv
import importlib.util
import math
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "summarize-telemetry.py"
SPEC = importlib.util.spec_from_file_location("summarize_telemetry", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class TelemetrySummaryTests(unittest.TestCase):
    def write_csv(self, rows):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "frames.csv"
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
        return path

    def test_warmup_percentiles_and_one_percent_low(self):
        rows = []
        for frame, milliseconds in enumerate([99.0, 10.0, 20.0, 30.0, 40.0]):
            rows.append(
                {
                    "build_id": "abc123",
                    "profile": "hardware-diagnostic",
                    "hardware_target": "New Nintendo 3DS",
                    "frame": frame,
                    "render_present_ms": milliseconds,
                    "citro_gpu_ms": milliseconds / 2,
                }
            )
        summary = MODULE.summarize(self.write_csv(rows), warmup=1, frames=4)
        self.assertEqual(summary["frames"], 4)
        self.assertEqual(summary["metrics"]["render_present_ms"]["p50"], 25.0)
        self.assertEqual(summary["performance"]["frames_over_16_67_ms"], 3)
        self.assertTrue(math.isclose(summary["performance"]["one_percent_low_fps"], 25.0))

    def test_rejects_empty_selection(self):
        path = self.write_csv([{"frame": 0, "render_present_ms": 10.0}])
        with self.assertRaisesRegex(ValueError, "no rows remain"):
            MODULE.summarize(path, warmup=1, frames=1)

    def test_timestamp_delta_is_actual_fps_and_dump_pause_is_excluded(self):
        rows = []
        for frame, timestamp in enumerate([0.0, 250.0, 500.0, 5000.0, 5250.0]):
            rows.append(
                {
                    "frame": frame,
                    "timestamp_ms": timestamp,
                    "render_present_ms": 80.0,
                }
            )
        summary = MODULE.summarize(self.write_csv(rows), warmup=0, frames=5)
        self.assertEqual(summary["performance"]["frame_time_metric"], "timestamp_delta_ms")
        self.assertEqual(summary["metrics"]["timestamp_delta_ms"]["mean"], 250.0)
        self.assertEqual(summary["performance"]["average_fps"], 4.0)


if __name__ == "__main__":
    unittest.main()
