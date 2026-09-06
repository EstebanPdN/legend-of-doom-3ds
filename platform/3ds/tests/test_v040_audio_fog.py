import ctypes
import math
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class AudioFogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        cls.directory = tempfile.TemporaryDirectory()
        path = Path(cls.directory.name)
        (path/'test.cpp').write_text('''
#include "src/common/audio/sound/pcm_wave.h"
#include "src/rendering/swrenderer/distance_fog_math.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "src/common/audio/sound/thirdparty/stb_vorbis.c"
extern "C" int pcm(const unsigned char*p,int size,int*out) {
 PcmWave w;if(!ReadPcmWave(p,size,w))return 0;
 out[0]=w.rate;out[1]=w.channels;out[2]=w.bits;out[3]=int(w.bytes);
 return 1;
}
extern "C" int ogg(const unsigned char*p,int size,int*out) {
 short*s=nullptr;int ch=0,rate=0;int n=stb_vorbis_decode_memory(p,size,&ch,&rate,&s);
 int peak=0;for(int i=0;i<n*ch;i++)peak=std::max(peak,std::abs(int(s[i])));
 free(s);out[0]=rate;out[1]=ch;out[2]=peak;out[3]=n;return n>0;
}
extern "C" double wall(double a,double b,double c,double d,double center,double fov,double x) {
 return WallSurfaceDistance(a,b,c,d,center,fov,x);
}
extern "C" double fog(double d,double s,double e) { return DistanceFogAmount(d,s,e); }
''')
        subprocess.run([compiler,'-std=c++17','-O2','-shared','-fPIC','-I',str(ROOT),str(path/'test.cpp'),'-o',str(path/'test.so')],check=True,capture_output=True)
        cls.lib = ctypes.CDLL(str(path/'test.so'))
        for name in ('pcm','ogg'):
            fn=getattr(cls.lib,name);fn.argtypes=[ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(ctypes.c_int)];fn.restype=ctypes.c_int
        cls.lib.wall.argtypes=[ctypes.c_double]*7;cls.lib.wall.restype=ctypes.c_double
        cls.lib.fog.argtypes=[ctypes.c_double]*3;cls.lib.fog.restype=ctypes.c_double

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_real_game_wav_and_ogg(self):
        folder=ROOT/'build-3ds/_deps/legend-of-doom/sounds'
        if not folder.exists(): self.skipTest('Fetch game assets first')
        files=sorted(p for p in folder.iterdir() if p.suffix.lower() in ('.wav','.ogg'))
        self.assertGreaterEqual(len(files),34)
        for p in files:
            with self.subTest(sound=p.name):
                data=p.read_bytes();out=(ctypes.c_int*4)()
                if p.suffix.lower()=='.wav':
                    self.assertEqual(self.lib.pcm(data,len(data),out),1)
                    with wave.open(str(p)) as w:
                        self.assertEqual(list(out),[w.getframerate(),w.getnchannels(),w.getsampwidth()*8,w.getnframes()*w.getnchannels()*w.getsampwidth()])
                else:
                    self.assertEqual(self.lib.ogg(data,len(data),out),1)
                    self.assertGreater(out[2],0)
                    self.assertIn(out[1],(1,2))

    def test_wave_chunks_and_truncation(self):
        fmt=struct.pack('<HHIIHH',1,1,32000,32000,1,8)
        chunks=b'JUNK'+struct.pack('<I',3)+b'abc\0'+b'fmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',4)+bytes([128,255,0,128])
        data=b'RIFF'+struct.pack('<I',len(chunks)+4)+b'WAVE'+chunks
        out=(ctypes.c_int*4)();self.assertEqual(self.lib.pcm(data,len(data),out),1)
        for n in range(len(data)):
            self.assertEqual(self.lib.pcm(data,n,out),0)
        damaged=bytearray(data);struct.pack_into('<I',damaged,16,0xffffffff)
        self.assertEqual(self.lib.pcm(bytes(damaged),len(damaged),out),0)

    def test_fog_on_actual_dump_walls_and_camera_angles(self):
        cases=[((6752.690767775,-2076.485177623),(7936,-2176),(2560,-2176)),
               ((9761.771631861,51.557199001),(9984,0),(8448,0)),
               ((9761.771631861,51.557199001),(8448,128),(10112,128))]
        checked=near=0
        def cross(a,b):return a[0]*b[1]-a[1]*b[0]
        for p,a,b in cases:
            for yaw in (0,45,90,135,180,225,270,315,350.386963):
                angle=math.radians(yaw);co,si=math.cos(angle),math.sin(angle)
                for tangent in (.7,1,1.3):
                    def view(v):
                        dx,dy=v[0]-p[0],v[1]-p[1]
                        return dx*si-dy*co,(dx*co+dy*si)*tangent
                    left,right=view(a),view(b)
                    for x in range(0,400,7):
                        lateral=(x+.5-200)/200*tangent
                        ray=(co+lateral*si,si-lateral*co);delta=(b[0]-a[0],b[1]-a[1]);relative=(a[0]-p[0],a[1]-p[1])
                        den=cross(ray,delta)
                        if abs(den)<1e-9:continue
                        along=cross(relative,delta)/den;fraction=cross(relative,ray)/den
                        if along<=0 or not 0<=fraction<=1:continue
                        expected=math.hypot(ray[0]*along,ray[1]*along)
                        actual=self.lib.wall(*left,*right,200,tangent,x)
                        self.assertAlmostEqual(actual,expected,places=5);checked+=1
                        if expected<=1152:
                            self.assertEqual(self.lib.fog(actual,1152,1536),0);near+=1
        self.assertGreater(checked,500);self.assertGreater(near,200)
        self.assertEqual(self.lib.fog(1536,1152,1536),1)
        self.assertEqual(self.lib.fog(1344,1152,1536),.5)
