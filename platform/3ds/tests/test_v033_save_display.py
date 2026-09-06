import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'src/common/platform/3ds/diagnostics_3ds.cpp'


class SaveDisplayTests(unittest.TestCase):
    def test_save_names_have_visible_case_sensitive_glyphs(self):
        table = SOURCE.read_text().split('constexpr FLoadingGlyph LoadingFont[] = {', 1)[1].split('\n};', 1)[0]
        glyphs = {c: [int(n) for n in rows.split(',')] for c, rows in
                  re.findall(r"\{ '(.)', \{ ([0-9, ]+) \} \}", table)}
        for c in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789<New Save>Link':
            if c == ' ':
                continue
            self.assertIn(c, glyphs)
            self.assertEqual(len(glyphs[c]), 7)
            self.assertTrue(any(glyphs[c]))
            self.assertTrue(all(0 <= row < 32 for row in glyphs[c]))
        self.assertNotEqual(glyphs['L'], glyphs['l'])

    def test_keyboard_preserves_gpu_image_in_both_buffers(self):
        compiler = shutil.which('c++')
        self.assertIsNotNone(compiler)
        function = SOURCE.read_text().split('void I_3DSPrepareNativeKeyboardTop()', 1)[1].split('\nvoid I_3DSFrameTelemetryBegin', 1)[0]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
using u16 = uint16_t;
constexpr int GFX_TOP=0, GFX_LEFT=0, GSP_RGBA8_OES=0;
#define R_FAILED(x) ((x)<0)
constexpr size_t bytes=400*240*4;
unsigned char buffers[2][bytes] = {};
unsigned back=1;
bool fenced=false, invalidated=false, badFormat=false;
int swaps=0;
struct Entry { uint32_t *framebuf0_vaddr; unsigned format, framebuf_widthbytesize; };
struct GSPGPU_CaptureInfo { Entry screencapture[2]; };
void I_PolyWaitForPresent3DS() { fenced=true; }
void gspWaitForVBlank() { assert(fenced); }
int GSPGPU_ImportDisplayCaptureInfo(GSPGPU_CaptureInfo *c) {
    assert(fenced);
    c->screencapture[0]={reinterpret_cast<uint32_t*>(buffers[0]), badFormat ? 1u : 0u, 960};
    return 0;
}
int GSPGPU_InvalidateDataCache(void *p, size_t n) {
    assert(fenced && n==bytes);
    // Emulate GPU pixels becoming visible to the CPU, including a dark image.
    std::memset(p, 1, n);
    invalidated=true;
    return 0;
}
unsigned char *gfxGetFramebuffer(int,int,u16 *w,u16 *h) {
    *w=240; *h=400; return buffers[back];
}
void I_3DSCleanDataCache(void *p,size_t n) {
    assert(invalidated);
    auto *b=static_cast<unsigned char*>(p);
    assert(std::all_of(b,b+n,[](unsigned char v){return v==1;}));
}
void gfxScreenSwapBuffers(int,bool) { back^=1; ++swaps; }
void I_3DSPrepareNativeKeyboardTop()
'''
        harness += function
        harness += r'''
int main() {
    I_3DSPrepareNativeKeyboardTop();
    assert(swaps==2);
    for (const auto &b:buffers)
        assert(std::all_of(b,b+bytes,[](unsigned char v){return v==1;}));
    badFormat=true; swaps=0; invalidated=false;
    I_3DSPrepareNativeKeyboardTop();
    assert(swaps==0 && !invalidated);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'capture.cpp'
            binary = Path(directory) / 'capture'
            source.write_text(harness)
            subprocess.run([compiler, '-std=c++17', str(source), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)
