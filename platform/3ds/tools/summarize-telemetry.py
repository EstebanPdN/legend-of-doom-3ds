#!/usr/bin/env python3
"""Summarize Legend of Doom 3DS frame telemetry without third-party modules."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path


IDENTITY_COLUMNS = {"build_id", "profile", "hardware_target"}
PREFERRED_METRICS = (
    "render_present_ms",
    "citro_cpu_ms",
    "citro_gpu_ms",
    "draw_calls",
    "input_triangles",
    "input_vertices",
    "output_triangles",
    "triangle_draws",
    "fan_draws",
    "strip_draws",
    "indexed_draws",
    "texture_changes",
    "texture_upload_bytes",
    "linear_free_bytes",
    "vram_free_bytes",
    "heap_used_bytes",
    "heap_free_bytes",
)


def percentile(values: list[float], percent: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("percentile of empty sequence")
    position = (len(ordered) - 1) * percent / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def load_rows(path: Path, warmup: int, frames: int | None) -> tuple[list[dict[str, str]], list[str]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise ValueError(f"{path}: missing CSV header")
        rows = list(reader)
        fields = reader.fieldnames
    if warmup < 0:
        raise ValueError("warm-up must be non-negative")
    rows = rows[warmup:]
    if frames is not None:
        rows = rows[:frames]
    if not rows:
        raise ValueError(f"{path}: no rows remain after warm-up/frame selection")
    return rows, fields


def numeric_metrics(rows: list[dict[str, str]], fields: list[str]) -> dict[str, list[float]]:
    metrics: dict[str, list[float]] = {}
    ordered_fields = [name for name in PREFERRED_METRICS if name in fields]
    ordered_fields += [
        name
        for name in fields
        if name not in ordered_fields
        and name not in IDENTITY_COLUMNS
        and name not in {"frame", "timestamp_ms"}
    ]
    for name in ordered_fields:
        values: list[float] = []
        valid = True
        for row in rows:
            raw = (row.get(name) or "").strip()
            try:
                value = float(raw)
            except ValueError:
                valid = False
                break
            if not math.isfinite(value):
                valid = False
                break
            values.append(value)
        if valid and values:
            metrics[name] = values
    return metrics


def timestamp_frame_times(rows: list[dict[str, str]]) -> list[float]:
    """Derive wall-clock frame times while excluding dump/debugger pauses."""
    timestamps: list[float] = []
    for row in rows:
        raw = (row.get("timestamp_ms") or "").strip()
        try:
            value = float(raw)
        except ValueError:
            return []
        if not math.isfinite(value):
            return []
        timestamps.append(value)
    deltas = [later - earlier for earlier, later in zip(timestamps, timestamps[1:])]
    deltas = [value for value in deltas if value > 0.0]
    if not deltas:
        return []
    # L+R+A pauses the game while screenshots and memory regions are written.
    # Those gaps are not rendered frames. Preserve genuinely slow gameplay,
    # but discard a discontinuity at least 5x the median and over one second.
    median = statistics.median(deltas)
    pause_threshold = max(1000.0, median * 5.0)
    return [value for value in deltas if value <= pause_threshold]


def summarize(path: Path, warmup: int = 120, frames: int | None = 600) -> dict[str, object]:
    rows, fields = load_rows(path, warmup, frames)
    metrics = numeric_metrics(rows, fields)
    wall_frame_times = timestamp_frame_times(rows)
    if wall_frame_times:
        metrics["timestamp_delta_ms"] = wall_frame_times
    result: dict[str, object] = {
        "source": str(path),
        "frames": len(rows),
        "warmup_frames": warmup,
    }
    for name in IDENTITY_COLUMNS:
        values = sorted({row.get(name, "") for row in rows if row.get(name, "")})
        if values:
            result[name] = values[0] if len(values) == 1 else values

    metric_summary: dict[str, dict[str, float]] = {}
    for name, values in metrics.items():
        metric_summary[name] = {
            "min": min(values),
            "p50": percentile(values, 50),
            "p95": percentile(values, 95),
            "p99": percentile(values, 99),
            "max": max(values),
            "mean": statistics.fmean(values),
        }
    result["metrics"] = metric_summary

    # timestamp_delta_ms measures the actual interval between completed frames.
    # render_present_ms is only one phase and overstated FPS by ~2-3x in dumps.
    total_name = (
        "timestamp_delta_ms"
        if "timestamp_delta_ms" in metrics
        else "frame_total_ms"
        if "frame_total_ms" in metrics
        else "render_present_ms"
    )
    if total_name in metrics:
        frame_times = metrics[total_name]
        slow_count = max(1, math.ceil(len(frame_times) * 0.01))
        slowest = sorted(frame_times, reverse=True)[:slow_count]
        mean_ms = statistics.fmean(frame_times)
        result["performance"] = {
            "frame_time_metric": total_name,
            "average_fps": 1000.0 / mean_ms if mean_ms > 0 else 0.0,
            "one_percent_low_fps": 1000.0 / statistics.fmean(slowest) if slowest and slowest[0] > 0 else 0.0,
            "frames_over_16_67_ms": sum(value > 16.67 for value in frame_times),
            "frames_over_33_33_ms": sum(value > 33.33 for value in frame_times),
        }
    return result


def print_text(summary: dict[str, object]) -> None:
    print(f"source: {summary['source']}")
    print(f"frames: {summary['frames']} (warm-up discarded: {summary['warmup_frames']})")
    for name in ("build_id", "profile", "hardware_target"):
        if name in summary:
            print(f"{name}: {summary[name]}")
    performance = summary.get("performance")
    if isinstance(performance, dict):
        print(
            "fps: average={:.2f} 1%low={:.2f} >16.67ms={} >33.33ms={}".format(
                performance["average_fps"],
                performance["one_percent_low_fps"],
                performance["frames_over_16_67_ms"],
                performance["frames_over_33_33_ms"],
            )
        )
    print("metric                         p50        p95        p99        max       mean")
    for name, values in summary["metrics"].items():
        print(
            f"{name:28} {values['p50']:10.3f} {values['p95']:10.3f} "
            f"{values['p99']:10.3f} {values['max']:10.3f} {values['mean']:10.3f}"
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="frame-telemetry.csv from the 3DS")
    parser.add_argument("--warmup", type=int, default=120, help="leading frames to discard (default: 120)")
    parser.add_argument("--frames", type=int, default=600, help="frames to analyze after warm-up (default: 600)")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args(argv)
    try:
        summary = summarize(args.csv, args.warmup, args.frames)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.json:
        json.dump(summary, sys.stdout, indent=2, sort_keys=True)
        print()
    else:
        print_text(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
