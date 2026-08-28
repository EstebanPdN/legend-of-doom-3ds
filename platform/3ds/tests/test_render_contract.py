#!/usr/bin/env python3
"""Offline render-contract checks for the GZDoom/NovaGL bridge."""

from __future__ import annotations

import math
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATCHES = ROOT / "patches"


def clip_polygon(vertices, distance):
    """Sutherland-Hodgman clip preserving position and UV attributes."""
    result = []
    for index, current in enumerate(vertices):
        following = vertices[(index + 1) % len(vertices)]
        dc = distance(current)
        dn = distance(following)
        current_inside = dc >= 0.0
        next_inside = dn >= 0.0
        if current_inside:
            result.append(current)
        if current_inside != next_inside:
            amount = dc / (dc - dn)
            result.append(tuple(a + amount * (b - a) for a, b in zip(current, following)))
    return result


def classify_eye_batch(w_values, epsilon=1.0 / 16.0):
    """Reference for NovaGL's cheap safe/crossing/behind classification."""
    inside = any(value >= epsilon for value in w_values)
    outside = any(value < epsilon for value in w_values)
    if outside and inside:
        return "clip"
    if outside:
        return "reject"
    return "safe"


class ProgramMatrixBridge:
    """Small reference model of the fixed-function program-state bridge."""

    identity = tuple(1.0 if index in (0, 5, 10, 15) else 0.0 for index in range(16))

    def __init__(self):
        self.programs = {}
        self.active_model = self.identity
        self.active_texture = self.identity
        self.projection = None
        self.loads = {"model": 0, "texture": 0, "projection": 0}

    def create(self, program):
        self.programs[program] = {"model": self.identity, "texture": self.identity}

    def use(self, program):
        state = self.programs[program]
        for name in ("model", "texture"):
            active_name = f"active_{name}"
            if getattr(self, active_name) != state[name]:
                setattr(self, active_name, state[name])
                self.loads[name] += 1

    def uniform(self, program, name, value):
        value = tuple(value)
        self.programs[program][name] = value
        active_name = f"active_{name}"
        if getattr(self, active_name) != value:
            setattr(self, active_name, value)
            self.loads[name] += 1

    def projection_uniform(self, value):
        value = tuple(value)
        if self.projection != value:
            self.projection = value
            self.loads["projection"] += 1


class RenderContractTests(unittest.TestCase):
    def test_program_switch_restores_model_and_texture(self):
        bridge = ProgramMatrixBridge()
        bridge.create(1)  # sky/model variant
        bridge.create(2)  # ordinary world variant
        sky_model = list(bridge.identity)
        sky_model[12] = 42.0
        sky_texture = list(bridge.identity)
        sky_texture[0] = 0.5

        bridge.use(1)
        bridge.uniform(1, "model", sky_model)
        bridge.uniform(1, "texture", sky_texture)
        bridge.use(2)

        self.assertEqual(bridge.active_model, bridge.identity)
        self.assertEqual(bridge.active_texture, bridge.identity)
        self.assertEqual(bridge.loads["model"], 2)
        self.assertEqual(bridge.loads["texture"], 2)

    def test_repeated_projection_upload_is_value_deduplicated(self):
        bridge = ProgramMatrixBridge()
        projection = tuple(float(index) for index in range(16))
        for _ in range(2595):
            bridge.projection_uniform(projection)
        self.assertEqual(bridge.loads["projection"], 1)

    def test_indexed_quad_preserves_fan_topology(self):
        fan = ((0, 1, 2), (0, 2, 3))
        indices = (0, 1, 2, 0, 2, 3)
        indexed = tuple(tuple(indices[offset : offset + 3]) for offset in (0, 3))
        self.assertEqual(indexed, fan)

    def test_eye_plane_clipping_removes_nonpositive_w_and_interpolates_uv(self):
        # (x, y, z, w, u, v): two wall vertices are behind the eye.
        quad = [
            (-1.0, -1.0, 0.0, -1.0, 0.0, 0.0),
            (1.0, -1.0, 0.0, 1.0, 1.0, 0.0),
            (1.0, 1.0, 0.0, 1.0, 1.0, 1.0),
            (-1.0, 1.0, 0.0, -1.0, 0.0, 1.0),
        ]
        epsilon = 1.0 / 16.0
        clipped = clip_polygon(quad, lambda vertex: vertex[3] - epsilon)

        self.assertEqual(len(clipped), 4)
        self.assertTrue(all(vertex[3] >= epsilon - 1e-12 for vertex in clipped))
        self.assertTrue(all(math.isfinite(value) for vertex in clipped for value in vertex))
        self.assertTrue(all(0.0 <= vertex[4] <= 1.0 and 0.0 <= vertex[5] <= 1.0 for vertex in clipped))

    def test_eye_crossing_triangle_is_clipped_to_homogeneous_side_planes(self):
        # One point sits just in front of the eye with a huge projected X/Y.
        polygon = [
            (1000.0, 1000.0, 0.0, 0.0625, 0.0, 0.0),
            (-0.5, -0.5, 0.0, 1.0, 1.0, 0.0),
            (0.5, -0.5, 0.0, 1.0, 0.5, 1.0),
        ]
        planes = (
            lambda vertex: vertex[3] - 1.0 / 16.0,
            lambda vertex: vertex[0] + vertex[3],
            lambda vertex: vertex[3] - vertex[0],
            lambda vertex: vertex[1] + vertex[3],
            lambda vertex: vertex[3] - vertex[1],
        )
        for plane in planes:
            polygon = clip_polygon(polygon, plane)

        self.assertGreaterEqual(len(polygon), 3)
        self.assertTrue(all(math.isfinite(value) for vertex in polygon for value in vertex))
        self.assertTrue(all(-vertex[3] <= vertex[0] <= vertex[3] for vertex in polygon))
        self.assertTrue(all(-vertex[3] <= vertex[1] <= vertex[3] for vertex in polygon))

    def test_eye_classifier_covers_floor_and_sprite_topologies(self):
        self.assertEqual(classify_eye_batch((4.0, 3.0, 2.0, 1.0)), "safe")
        self.assertEqual(classify_eye_batch((-4.0, -3.0, -2.0, -1.0)), "reject")
        self.assertEqual(classify_eye_batch((-1.0, 1.0, 2.0, -2.0)), "clip")

    def test_persistent_patches_contain_the_runtime_contract(self):
        state = (PATCHES / "novagl-gzdoom-state-dedup.patch").read_text(encoding="utf-8")
        quads = (PATCHES / "novagl-gzdoom-indexed-quads.patch").read_text(encoding="utf-8")
        eye = (PATCHES / "novagl-gzdoom-eyeclip.patch").read_text(encoding="utf-8")
        frustum = (PATCHES / "novagl-hardware-frustum-guard.patch").read_text(
            encoding="utf-8"
        )

        self.assertIn("GzdProgramState", state)
        self.assertIn("gzd_load_matrix(GL_TEXTURE", state)
        self.assertIn("memcmp(gzd_projection", state)
        self.assertIn("C3D_DrawElements(GPU_TRIANGLES, 6", quads)
        self.assertIn("mode == GL_TRIANGLE_FAN || mode == GL_TRIANGLE_STRIP", quads)
        self.assertIn("count == 4 && g.static_quad_indices", quads)
        self.assertIn("force_eye_clip", eye)
        self.assertIn("mode == GL_TRIANGLES || mode == GL_TRIANGLE_FAN", eye)
        self.assertIn("mode == GL_TRIANGLE_STRIP", eye)
        self.assertIn("if (!eye_inside) return 3", eye)
        self.assertIn("eye_epsilon = 1.0f / 16.0f", eye)
        self.assertIn("x + w >= 0", frustum)
        self.assertIn("w - x >= 0", frustum)
        self.assertIn("y + w >= 0", frustum)
        self.assertIn("w - y >= 0", frustum)
        self.assertIn("Count first, then reserve exactly", frustum)
        self.assertIn("never submit NaN/Inf", frustum)

    def test_hardware_vram_uploads_use_linear_staging_and_gx_copy(self):
        vram = (PATCHES / "novagl-hardware-vram-upload.patch").read_text(
            encoding="utf-8"
        )

        self.assertIn("texture_data_is_vram", vram)
        self.assertIn("C3D_TexLoadImage(tex, vram_staging", vram)
        self.assertIn("C3D_TexLoadImage(&slot->tex, staging", vram)
        self.assertIn("ensure_texture_cpu_writable", vram)
        self.assertIn("GX_TRANSFER_RAW_COPY(1)", vram)
        self.assertIn("linearAlloc((size_t) size)", vram)
        self.assertIn("linearFree(g.tex_staging)", vram)


if __name__ == "__main__":
    unittest.main()
