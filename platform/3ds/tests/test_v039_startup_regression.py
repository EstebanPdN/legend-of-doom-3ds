import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class StartupRegressionTests(unittest.TestCase):
    def test_callbacks_terminate_when_assignment_reenters(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        def body(path, name):
            text = (ROOT / path).read_text()
            return re.search(r'CUSTOM_CVAR\([^\n]*' + name + r'[^\n]*\)\n(\{.*?\n\})', text, re.S)[1]
        scale = body('src/common/rendering/r_videoscale.cpp', 'lod3ds_render_scale')
        distance = body('src/rendering/swrenderer/r_swcolormaps.cpp', 'lod3ds_render_distance')
        prefix = '''#include <algorithm>
#include <cmath>
#include <cassert>
#include <limits>
using std::clamp;
// FBaseCVar::ForceSet invokes callbacks even when the value is unchanged.
template<class T> struct Cvar {
  T value; void (*callback)(Cvar&); int depth=0, calls=0;
  operator T() const { return value; }
  T operator=(T next) {
    value=next;
    if (++depth > 4) throw 1;
    ++calls; callback(*this); --depth; return value;
  }
};
int vid_scalemode, vid_scale_customwidth, vid_scale_customheight;
float vid_scale_custompixelaspect;
bool setsizeneeded;
double Map01DistanceFogStart, Map01DistanceFogEnd;
'''
        suffix = '''
int main() {
 try {
  for (float input : {5.f,5.5f,8.f,10.f,5.26f,0.f,20.f,std::numeric_limits<float>::quiet_NaN()}) {
    Cvar<float> value{input,change_scale}; change_scale(value);
    assert(value.calls<=1 && value.depth==0);
    assert(value.value>=5 && value.value<=10);
    assert(vid_scale_customwidth==int(value.value*40));
    assert(vid_scale_customheight==int(value.value*24));
  }
  for (int input : {-1,0,1,2,3}) {
    Cvar<int> value{input,change_distance}; change_distance(value);
    assert(value.calls<=1 && value.depth==0);
    assert(Map01DistanceFogEnd==1536+value.value*512);
    assert(Map01DistanceFogStart==Map01DistanceFogEnd*.75);
  }
 } catch (...) { return 1; }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory)/'callback.cpp', Path(directory)/'callback'
            for broken in (None, 'scale', 'distance'):
                scale_body = '{ self = float(self); }' if broken == 'scale' else scale
                distance_body = '{ self = int(self); }' if broken == 'distance' else distance
                source.write_text(prefix + 'void change_scale(Cvar<float>& self)' + scale_body +
                                  'void change_distance(Cvar<int>& self)' + distance_body + suffix)
                subprocess.run([compiler,'-std=c++17',str(source),'-o',str(binary)],check=True,capture_output=True)
                result = subprocess.run([str(binary)],capture_output=True)
                self.assertEqual(result.returncode, 1 if broken else 0)

    def test_home_audio_duration_limit(self):
        spec = importlib.util.spec_from_file_location('banner_audio', ROOT/'platform/3ds/tools/validate-banner-audio.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertLessEqual(module.validate(ROOT/'platform/3ds/assets/banner.wav'), 3)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'banner.wav'
            for frames, valid in [(96000,True),(96001,False),(104410,False)]:
                with wave.open(str(path),'wb') as wav:
                    wav.setparams((2,2,32000,frames,'NONE','not compressed'))
                    wav.writeframes(bytes(frames*4))
                if valid:
                    self.assertEqual(module.validate(path),3)
                else:
                    with self.assertRaises(ValueError): module.validate(path)
