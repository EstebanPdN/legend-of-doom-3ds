import ctypes
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class MapHeartTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        source = (ROOT/'src/common/platform/3ds/diagnostics_3ds.cpp').read_text()
        body = source.split('void DrawBottomHeartsAndCounters(', 1)[1].split('{', 1)[1].split('\n\tFGameTexture *face', 1)[0]
        ticker = (ROOT/'src/p_tick.cpp').read_text()
        pause = re.search(r'bool P_CheckTickerPaused \(\)\n\{.*?\n\}', ticker, re.S)[0]
        code = '''
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include "src/common/platform/3ds/bottom_game_interface.inc"
struct FEmbeddedBottomImage { unsigned Width,Height,PixelCount; const uint8_t *Data; size_t DataSize; };
#define BOTTOM_IMAGE(n) {n##Width,n##Height,n##PixelCount,n##Data,sizeof(n##Data)}
struct AActor {int health, maximum; int GetMaxHealth(){return maximum;} };
int *rects; int count;
void DrawEmbeddedBottomImageSized(unsigned char*,int x,int y,const FEmbeddedBottomImage &image,int w,int h) {
 int *r=rects+4+count++*5; r[0]=x;r[1]=y;r[2]=w;r[3]=h;
 r[4]=image.Data==BottomHeartFullData ? 4 : image.Data==BottomHeartThreeQuarterData ? 3 : image.Data==BottomHeartHalfData ? 2 : image.Data==BottomHeartQuarterData ? 1 : 0;
}
extern "C" void layout(int maximum,int current,int*out) {
 rects=out;count=0;AActor actor{current,maximum}; AActor *owner=&actor;unsigned char *framebuffer=nullptr;
'''+body+'''
 out[0]=count;out[1]=faceY;out[2]=counterStartY;out[3]=heartsBottom;
}
bool netgame,demoplayback,demorecording,automapactive;int gamestate,wipegamestate,menuactive,ConsoleState,consoleplayer=0;
constexpr int GS_TITLELEVEL=2, MENU_Off=0, MENU_OnNoPause=2,c_up=0,c_down=1,c_falling=2, NO_VALUE=-999;
constexpr int LEVEL2_PAUSE_MUSIC_IN_MENUS=1;
struct Player {int viewz;} players[1];struct Level {int flags2;} level;
Level *primaryLevel=&level;int calls;bool keepMusic;
void S_PauseSound(bool music,bool){++calls;keepMusic=music;}
'''+pause+'''
extern "C" int check(int map,int menu,int console,int network,int playback,int recording,int title,int ready,int wipe,int pauseMusic) {
 automapactive=map;menuactive=menu;ConsoleState=console;netgame=network;demoplayback=playback;demorecording=recording;
 gamestate=title?GS_TITLELEVEL:1;wipegamestate=wipe?0:gamestate;players[0].viewz=ready?41:NO_VALUE;level.flags2=pauseMusic;
 calls=0;bool result=P_CheckTickerPaused();return result+2*calls+4*(calls&&keepMusic);
}
'''
        (folder/'test.cpp').write_text(code)
        subprocess.run([compiler,'-std=c++17','-D__3DS__','-shared','-fPIC','-I',str(ROOT),str(folder/'test.cpp'),'-o',str(folder/'test.so')],check=True,capture_output=True)
        cls.lib=ctypes.CDLL(str(folder/'test.so'))
        cls.lib.layout.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.POINTER(ctypes.c_int)]
        cls.lib.check.argtypes=[ctypes.c_int]*10

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def geometry(self, hearts, health=None):
        out=(ctypes.c_int*104)()
        self.lib.layout(hearts*8,hearts*8 if health is None else health,out)
        return list(out[:4]), [list(out[4+i*5:9+i*5]) for i in range(out[0])]

    def test_map_pause_and_close_preserve_other_pause_reasons(self):
        def check(**kw):
            args=dict(map=0,menu=0,console=0,network=0,playback=0,recording=0,title=0,ready=1,wipe=0,pauseMusic=0)
            args.update(kw)
            return self.lib.check(*args.values())
        self.assertEqual(check(),0)
        self.assertEqual(check(map=1),7)
        self.assertEqual(check(),0)
        for reason in ('network','playback','recording','title','wipe'):
            self.assertEqual(check(map=1,**{reason:1}),0,reason)
        self.assertEqual(check(map=1,ready=0),0)
        self.assertEqual(check(map=1,pauseMusic=1),3)
        self.assertEqual(check(menu=1),7)
        self.assertEqual(check(console=1),7)
        self.assertEqual(check(menu=2),0)
        self.assertEqual(check(map=1,menu=2),7)

    def test_all_heart_counts_fit_and_partial_rows_are_centered(self):
        for hearts in range(1,21):
            meta,rects=self.geometry(hearts)
            count,face,counters,bottom=meta
            self.assertEqual(count,hearts)
            self.assertGreaterEqual(face,bottom+6)
            self.assertGreaterEqual(counters,face+55+6)
            self.assertLessEqual(abs((face-bottom)-(counters-face-55)),1)
            self.assertEqual(counters,max(157,max(76,bottom+6)+69))
            self.assertLessEqual(counters+36+11,218)
            rows={}
            for x,y,w,h,fill in rects:
                self.assertGreaterEqual(x,239)
                self.assertLessEqual(x+w,303)
                self.assertGreaterEqual(y,30)
                self.assertLessEqual(y+h,face-6)
                self.assertEqual(fill,4)
                rows.setdefault(y,[]).append((x,w))
            for row in rows.values():
                center=(row[0][0]+row[-1][0]+row[-1][1])/2
                self.assertLessEqual(abs(center-271),1)
                for (x,w),(next_x,_) in zip(row,row[1:]):
                    self.assertLess(x+w,next_x)
        _,rects=self.geometry(16)
        self.assertEqual(len({r[1] for r in rects}),4)
        self.assertEqual({sum(r[1]==y for r in rects) for y in {r[1] for r in rects}},{4})
        self.assertEqual({(r[2],r[3]) for r in rects},{(11,12)})

    def test_partial_health_remains_visible(self):
        for health,expected in [(0,[0,0,0]),(2,[1,0,0]),(4,[2,0,0]),(6,[3,0,0]),(10,[4,1,0]),(24,[4,4,4])]:
            _,rects=self.geometry(3,health)
            self.assertEqual([r[4] for r in rects],expected)
