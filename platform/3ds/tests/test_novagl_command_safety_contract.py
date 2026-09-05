#!/usr/bin/env python3
"""Offline contracts for the physical-PICA command safety recovery patch."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "platform/3ds/patches/novagl-hardware-command-safety.patch"
RANGE_PROBE_PATCH = ROOT / "platform/3ds/patches/novagl-hardware-draw-range-probe.patch"


class NovaGLCommandSafetyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text(encoding="utf-8")
        cls.added = "\n".join(
            line[1:]
            for line in cls.patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )

    def test_patch_is_last_in_both_dependency_stacks(self):
        for script_name in ("fetch-dependencies.sh", "test-patches.sh"):
            script = (ROOT / "platform/3ds" / script_name).read_text(
                encoding="utf-8"
            )
            self.assertGreater(
                script.index("novagl-hardware-command-safety.patch"),
                script.index("novagl-hardware-cache-fallback.patch"),
            )
            self.assertGreater(
                script.index("novagl-hardware-draw-range-probe.patch"),
                script.index("novagl-hardware-command-safety.patch"),
            )

    def test_sparse_range_probe_never_waits_or_resets_the_queue(self):
        probe = RANGE_PROBE_PATCH.read_text(encoding="utf-8")
        added = "\n".join(
            line[1:]
            for line in probe.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )
        for token in (
            "NOVA_DIAG_SEGMENT_START",
            "NOVA_DIAG_SEGMENT_END",
            "NOVA_DIAG_SEGMENT_STRIDE",
            "nova_hw_watchdog_frame_split(GX_CMDLIST_FLUSH",
        ):
            self.assertIn(token, added)
        self.assertNotIn("gxCmdQueueWait", added)
        self.assertNotIn("gxCmdQueueStop", added)
        self.assertNotIn("gxCmdQueueClear", added)

    def test_fast_math_cannot_remove_finite_guards(self):
        self.assertIn("nova_float_is_finite", self.added)
        self.assertIn("0x7f800000", self.added)
        self.assertIn("nova_mtx_is_finite", self.added)
        self.assertNotIn("isfinite(", "\n".join(
            line for line in self.added.splitlines()
            if not line.lstrip().startswith("/*")
            and not line.lstrip().startswith("*")
        ))

    def test_malformed_geometry_is_dropped_before_pica(self):
        for token in (
            "nova_vbo_array_range_valid",
            "max_gather_index",
            "return 3; /* malformed range",
            "vertex_stream_invalid",
            "BufInfo_Add(bufInfo, base, stride, 3, 0x210) < 0",
            "if (g.vertex_stream_invalid) return;",
        ):
            self.assertIn(token, self.added)

    def test_unsafe_clip_fallback_is_dropped(self):
        self.assertIn("if (!out) return 1;", self.added)
        self.assertIn("side_crossing", self.added)
        self.assertIn("nova_float_is_finite(clipped.x)", self.added)

    def test_empty_raster_state_never_encodes_underflow_or_infinity(self):
        self.assertIn("g.viewport_empty", self.added)
        self.assertIn("width == 0 || height == 0", self.added)
        self.assertIn("g.scissor_empty", self.added)
        self.assertIn("x1 <= x0 || y1 <= y0", self.added)
        self.assertIn("nova_draw_region_discarded", self.added)

    def test_fast_paths_obey_the_same_draw_discard_contract(self):
        self.assertGreaterEqual(
            self.added.count("if (nova_draw_region_discarded()) return;"), 3
        )
        self.assertIn("if (g.vertex_stream_invalid) return;", self.added)

    def test_immediate_and_float_colors_drop_nonfinite_batches(self):
        for token in (
            "imm.invalid = 0",
            "imm.vertex_count == 0 || imm.invalid",
            "nova_float_is_finite(imm.current_texcoord[0])",
            "if (!nova_float_is_finite(cf[ch]))",
            "cleanup_vbo_stream();",
        ):
            self.assertIn(token, self.added)

    def test_tev_and_fog_float_to_integer_paths_are_sanitized(self):
        for token in (
            "(double) param < (double) INT_MIN",
            "sanitize_tev_color_component",
            "sanitized[i].constant_color[channel]",
            "if (param == (GLfloat) GL_LINEAR)",
        ):
            self.assertIn(token, self.added)

    def test_internal_quad_draws_only_report_real_submissions(self):
        self.assertGreaterEqual(
            self.added.count("if (idx_staged && !g.vertex_stream_invalid)"), 2
        )

    def test_supervisor_cannot_overflow_the_fixed_gx_queue(self):
        self.assertIn("validated-192k-bounded-segments", self.added)
        self.assertIn("return normal_words;", self.added)
        self.assertNotIn("NOVA_HW_DIAG_SEGMENT_BYTES", self.added)
        manifest = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn("0x40000 * 3 / 4", manifest)
        self.assertNotIn('printf %u "$((64 * 1024))"', manifest)

    def test_uniforms_and_fog_reject_nonfinite_derived_values(self):
        for token in (
            "gzd_floats_finite",
            "fog_values_ok",
            "linear_fog_values_ok",
            "exp_fog_values_ok",
            "nova_float_is_finite(fog_inv_range)",
            "nova_float_is_finite(fog_density_far)",
            "neutral_mul",
            "neutral_add",
        ):
            self.assertIn(token, self.added)
        self.assertNotIn(
            "g.fog_density * g.fog_density, 2.0f", self.added
        )

    def test_index_and_ring_arithmetic_is_checked_before_use(self):
        for token in (
            "count - 1 > INT_MAX - first",
            "max_index > (uint32_t)INT_MAX",
            "SIZE_MAX / index_size",
            "size > capacity - *offset",
            "vertex_count <= max_batch_verts - s_batch.total_verts",
            "_Static_assert(sizeof(NovaClipV) == 28",
            "out_count > max_out - produced",
        ):
            self.assertIn(token, self.added)

    def test_basevertex_resolves_ebo_and_promotes_without_truncation(self):
        for token in (
            "source_bytes",
            "const int64_t value",
            "value < 0 || (uint64_t)value > UINT32_MAX",
            "const GLuint saved_ebo",
            "GL_UNSIGNED_INT, shifted",
        ):
            self.assertIn(token, self.added)

    def test_first_frame_watchdog_uses_progress_and_real_ticks(self):
        self.assertIn("NOVA_HW_STALL_TICKS", self.added)
        self.assertIn("now != completed", self.added)
        self.assertIn("render-queue-no-progress-5s", self.added)
        self.assertIn("svcSleepThread(1000000)", self.added)
        self.assertNotIn("gxCmdQueueWait(q, (s64)2000000000LL)", self.added)

    def test_command_dump_correlates_virtual_queue_addresses(self):
        self.assertIn("gpu-command-segment.bin", self.added)
        self.assertIn("nova_hw_find_draw_for_vaddr", self.added)
        self.assertIn("r->cmd_vaddr != stalled_vaddr", self.added)
        self.assertIn("remove(s_hw_cmd_dump_path)", self.added)
        self.assertNotIn("cmd_paddr != stalled", self.added)

    def test_scanout_format_and_binary_search_cutoff_are_wired(self):
        self.assertIn("GX_TRANSFER_FMT_RGBA8", self.added)
        self.assertIn("NOVAGL_DIAG_DRAW_CUTOFF", self.added)
        self.assertIn("nova_diag_draw_allowed", self.added)

    def test_engine_flood_projection_cannot_emit_divide_by_zero_geometry(self):
        source = (
            ROOT / "src/rendering/hwrenderer/scene/hw_renderhacks.cpp"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "src/rendering/hwrenderer/scene/hw_drawinfo.h"
        ).read_text(encoding="utf-8")
        self.assertIn("FloodFloatIsFinite", source)
        self.assertIn("denominator1 == 0.0f || denominator2 == 0.0f", source)
        self.assertIn("if (!CreateFloodPoly", source)
        self.assertIn("bool CreateFloodPoly", header)


if __name__ == "__main__":
    unittest.main()
