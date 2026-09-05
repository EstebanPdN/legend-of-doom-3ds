#!/usr/bin/env python3
"""Build the small 3DS aim marker from the artist-provided PNG."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("asset", type=Path)
    parser.add_argument("include", type=Path)
    args = parser.parse_args()

    image = Image.open(args.source).convert("RGBA")
    image = image.resize((17, 17), Image.Resampling.LANCZOS)
    args.asset.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.asset)

    alpha = list(image.getchannel("A").getdata())
    rows = []
    for offset in range(0, len(alpha), 17):
        rows.append("\t" + ", ".join(f"{value:3d}" for value in alpha[offset:offset + 17]) + ",")
    text = (
        "// Generated from platform/3ds/assets/aim-crosshair.png.\n"
        "// Run platform/3ds/tools/generate-aim-crosshair.py to refresh it.\n"
        "static constexpr int AimCrosshairWidth = 17;\n"
        "static constexpr int AimCrosshairHeight = 17;\n"
        "static constexpr uint8_t AimCrosshairAlpha[] = {\n"
        + "\n".join(rows)
        + "\n};\n"
    )
    args.include.parent.mkdir(parents=True, exist_ok=True)
    args.include.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
