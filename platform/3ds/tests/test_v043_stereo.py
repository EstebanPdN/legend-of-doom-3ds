import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def run_cpp(code):
    compiler=shutil.which('c++')
    if not compiler: raise unittest.SkipTest('C++ compiler unavailable')
    with tempfile.TemporaryDirectory() as name:
        path=Path(name);(path/'test.cpp').write_text(code)
        subprocess.run([compiler,'-std=c++17','-I',str(ROOT),str(path/'test.cpp'),'-o',str(path/'test')],check=True,capture_output=True)
        subprocess.run([str(path/'test')],check=True)


class StereoTests(unittest.TestCase):
    def test_eye_buffers_scaling_overlays_and_mode_reset(self):
        run_cpp('''
#include "src/common/platform/3ds/stereo_3ds.h"
#include <cassert>
#include <limits>
int main() {
 lod3ds::StereoFrame pair;
 assert(!pair.Begin(401,240,401,1));assert(!pair.Begin(100,240,99,1));
 assert(!pair.Begin(100,241,100,1));assert(!pair.Begin(100,240,100,0));
 assert(!pair.Begin(100,240,100,std::numeric_limits<float>::quiet_NaN()));
 for(int w: {200,280,400}) {
  int h=w*3/5,p=w+8;assert(pair.Begin(w,h,p,1));
  std::vector<uint32_t> data(p*h,0x12345678);
  for(int y=0;y<h;y++) for(int x=0;x<w;x++) data[y*p+x]=0xff000000|(y<<8)|(x%128);
  auto original=data;pair.ShiftWorld(reinterpret_cast<uint8_t *>(data.data()),-2.5);
  for(int y=0;y<h;y++) {
   for(int x=3;x<w-3;x++) {
    assert(((data[y*p+x]>>8)&255)==unsigned(y));
    assert((data[y*p+x]>>24)==255);
   }
   for(int x=w;x<p;x++)assert(data[y*p+x]==0x12345678);
  }
  pair.Capture(reinterpret_cast<uint8_t *>(data.data()),true);
  auto left=data;data=original;pair.ShiftWorld(reinterpret_cast<uint8_t *>(data.data()),2.5);
  pair.Capture(reinterpret_cast<uint8_t *>(data.data()),false);assert(pair.ready);
  auto right=data;data[20*p+30]=0xffff00ff;
  auto output=reinterpret_cast<const uint32_t *>(pair.ComposeLeft(reinterpret_cast<uint8_t *>(data.data()),w,h,p*4));
  assert(output && output[20*p+30]==0xffff00ff);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(y*p+x!=20*p+30)assert(output[y*p+x]==left[y*p+x]);
  assert(!pair.ComposeLeft(reinterpret_cast<uint8_t *>(data.data()),w-1,h,p*4));
  std::fill(data.begin(),data.end(),0xffeeeeee);
  output=reinterpret_cast<const uint32_t *>(pair.ComposeLeft(reinterpret_cast<uint8_t *>(data.data()),w,h,p*4));
  assert(std::equal(data.begin(),data.end(),output));
  pair.Reset();assert(!pair.ready && pair.strength==0);
  assert(!pair.ComposeLeft(reinterpret_cast<uint8_t *>(data.data()),w,h,p*4));
 }
}
''')

    def test_parallel_camera_projection_is_inward_and_has_no_vertical_shift(self):
        # Independent pinhole projection of the two camera positions used by RenderActorView.
        for width in (200,280,400):
            for focal in (width*.35,width*.5,width*.8):
                for strength in (0,.1,.5,1):
                    shift=4*strength*width/400
                    for convergence in (0,.01,4,32,96):
                        separation=shift*convergence/focal
                        for z in (max(convergence,.01),max(convergence,.01)*2,1536,2560):
                            for x,y in ((0,0),(10,20),(-20,-15)):
                                left=width/2+focal*(x+separation)/z-shift
                                right=width/2+focal*(x-separation)/z+shift
                                disparity=(right-left)*400/width
                                self.assertGreaterEqual(disparity,-1e-8)
                                self.assertLessEqual(disparity,8*strength+1e-8)
        source=(ROOT/'src/rendering/swrenderer/scene/r_scene.cpp').read_text()
        loop=source.split('for (int eye=0; eye<2; ++eye)',1)[1].split('return;',1)[0]
        self.assertNotIn('R_SetupFrame',loop)
        self.assertNotIn('Pos.Z +=',loop)
        self.assertNotIn('Angles.',loop)
        self.assertLess(loop.index('pair.ShiftWorld'),loop.index('RenderPSprites();'))
        self.assertIn('DrawerThreads::WaitForWorkers();',loop)

    def test_bottom_title_zoom_is_centered_cached_and_bounded(self):
        source=(ROOT/'src/common/platform/3ds/diagnostics_3ds.cpp').read_text()
        function=re.search(r'void DrawMenuBottomScreen\(.*?\n\}',source,re.S)[0]
        run_cpp('''
#include <vector>
#include <cstring>
#include <cassert>
#include <cmath>
constexpr int BottomScreenWidth=320,BottomScreenHeight=240;
int decodes;
void DecodeMenuBottomScreen(unsigned char *p,unsigned brightness) {
 ++decodes;auto pixels=reinterpret_cast<unsigned *>(p);
 for(int x=0;x<320;++x)for(int y=0;y<240;++y)pixels[x*240+239-y]=x|(y<<9)|(brightness<<18);
}
'''+function+'''
int main(){
 std::vector<unsigned> out(320*240);
 DrawMenuBottomScreen(reinterpret_cast<unsigned char *>(out.data()));assert(decodes==1);
 for(int x=0;x<320;++x)for(int y=0;y<240;++y){
  unsigned color=out[x*240+239-y];int sx=color&511,sy=(color>>9)&511;
  assert(sx>=7 && sx<=312 && sy>=5 && sy<=234);
  assert(std::abs(sx+0.5-((x+.5-160)/1.05+160))<=0.5);assert(std::abs(sy+0.5-((y+.5-120)/1.05+120))<=0.5);
  unsigned opposite=out[(319-x)*240+y];assert(sx+int(opposite&511)==319);assert(sy+int((opposite>>9)&511)==239);
 }
 DrawMenuBottomScreen(reinterpret_cast<unsigned char *>(out.data()));assert(decodes==1);
 DrawMenuBottomScreen(reinterpret_cast<unsigned char *>(out.data()),128);assert(decodes==2);
}
''')

    def test_choice_labels_are_centered_as_a_group(self):
        source=(ROOT/'src/common/platform/3ds/diagnostics_3ds.cpp').read_text()
        fragment=source.split('const int leftWidth =',1)[1].split('DrawBottomScaledFontText(framebuffer, font, leftX',1)[0]
        run_cpp('''
#include <cmath>
#include <cstring>
#include <cassert>
int NativeFontTextWidth(void*,const char *s){return int(std::strlen(s))*8;}
void check(const char*left,const char*right){
 void *font=nullptr;float Scale=1.28f;
 const int leftWidth ='''+fragment+'''
 assert(std::abs((leftX+rightX+rightWidth)-320)<=1);
 assert(rightX-(leftX+leftWidth)==48);assert(leftX>=40);assert(rightX+rightWidth<=280);
 assert(leftX+leftWidth<160 && rightX>160);
}
int main(){check("LOAD","DELETE");check("YES","NO");}
''')
