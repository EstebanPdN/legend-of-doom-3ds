import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class RenderSettingsTests(unittest.TestCase):
    def test_runtime_callbacks_and_canvas_sizes(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        scale = (ROOT / 'src/common/rendering/r_videoscale.cpp').read_text()
        fog = (ROOT / 'src/rendering/swrenderer/r_swcolormaps.cpp').read_text()
        def callback(text, name):
            return re.search(r'CUSTOM_CVAR\([^\n]*' + name + r'[^\n]*\)\n(\{.*?\n\})', text, re.S)[1]
        def function(name):
            return re.search(r'int ' + name + r'\(\)\n\{.*?\n\}', scale, re.S)[0]
        code = '''#include <algorithm>
#include <cmath>
#include <cassert>
#include <limits>
using std::clamp;
float lod3ds_render_scale;
int vid_scalemode, vid_scale_customwidth, vid_scale_customheight;
float vid_scale_custompixelaspect;
bool setsizeneeded;
constexpr int GameplayDisplayWidth = 400, GameplayDisplayHeight = 240;
double Map01DistanceFogStart, Map01DistanceFogEnd;
'''
        code += 'void change_scale(float& self)' + callback(scale, 'lod3ds_render_scale')
        code += 'void change_distance(int& self)' + callback(fog, 'lod3ds_render_distance')
        code += function('I_3DSGameplayResolutionWidth')
        code += function('I_3DSGameplayResolutionHeight')
        code += '''
int main() {
  for (int i=0; i<=10; ++i) {
    lod3ds_render_scale = 5 + i * .5f;
    change_scale(lod3ds_render_scale);
    assert(I_3DSGameplayResolutionWidth() == 200 + 20*i);
    assert(I_3DSGameplayResolutionHeight() == 120 + 12*i);
    assert(vid_scale_customwidth == I_3DSGameplayResolutionWidth());
    assert(vid_scale_customheight == I_3DSGameplayResolutionHeight());
    assert(setsizeneeded);
  }
  for (float old : {5.f,8.f,10.f}) { float value=old; change_scale(value); assert(value==old); }
  float value=5.26f; change_scale(value); assert(value==5.5f);
  value=0; change_scale(value); assert(value==5);
  value=20; change_scale(value); assert(value==10);
  value=std::numeric_limits<float>::quiet_NaN(); change_scale(value); assert(value==7);
  for (int level=0; level<=2; ++level) {
    int value=level; change_distance(value);
    assert(Map01DistanceFogEnd==1536+level*512);
    assert(Map01DistanceFogStart==Map01DistanceFogEnd*.75);
  }
  int normal=1; change_distance(normal);
  assert(Map01DistanceFogStart==1536 && Map01DistanceFogEnd==2048);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'settings.cpp'
            binary = Path(directory) / 'settings'
            source.write_text(code)
            subprocess.run([compiler, '-std=c++17', str(source), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)
