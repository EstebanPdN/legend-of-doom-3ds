#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace lod3ds
{
struct StereoFrame
{
    std::vector<uint32_t> left, right, row;
    int width = 0, height = 0, pitch = 0;
    bool ready = false;
    float strength = 0;
    double convergence = 0, separation = 0;

    void Reset() { ready = false; strength = 0; convergence = separation = 0; }
    bool Begin(int w, int h, int p, float value)
    {
        Reset();
        if (w < 1 || w > 400 || h < 1 || h > 240 || p < w || p > 1024 ||
            !std::isfinite(value) || value <= 0) return false;
        try { left.resize(p*h); right.resize(p*h); row.resize(w); }
        catch (const std::bad_alloc &) { return false; }
        width=w; height=h; pitch=p; strength=std::min(value,1.f);
        return true;
    }
    void Capture(const uint8_t *pixels, bool first)
    {
        std::memcpy((first ? left : right).data(), pixels, size_t(pitch)*height*4);
        if (!first) ready=true;
    }
    void ShiftWorld(uint8_t *pixels, double shift)
    {
        const int border=int(std::ceil(std::abs(shift)));
        for (int y=0; y<height; ++y)
        {
            auto dst=reinterpret_cast<uint32_t *>(pixels)+y*pitch;
            std::memcpy(row.data(),dst,width*4);
            for (int x=0;x<width;++x)
            {
                if (x<border || x>=width-border) { dst[x]=0xff000000; continue; }
                const double source=std::clamp(x-shift,0.0,double(width-1));
                const int a=int(source), b=std::min(a+1,width-1);
                const unsigned fraction=unsigned((source-a)*256);
                uint32_t color=0;
                for (unsigned channel=0;channel<32;channel+=8)
                    color |= ((((row[a]>>channel)&255)*(256-fraction)+
                        ((row[b]>>channel)&255)*fraction+128)>>8)<<channel;
                dst[x]=color;
            }
        }
    }
    const uint8_t *ComposeLeft(const uint8_t *finalPixels,int w,int h,int bytePitch)
    {
        if (!ready || width!=w || height!=h || pitch*4!=bytePitch) return nullptr;
        const auto final=reinterpret_cast<const uint32_t *>(finalPixels);
        size_t changed=0;
        for (int y=0;y<h;++y) for (int x=0;x<w;++x)
        {
            const int i=y*pitch+x;
            if (final[i]!=right[i]) { left[i]=final[i]; ++changed; }
        }
        // Full-screen flashes remain identical in both eyes.
        if (changed>size_t(w)*h/2) std::memcpy(left.data(),finalPixels,size_t(pitch)*h*4);
        return reinterpret_cast<const uint8_t *>(left.data());
    }
};
inline StereoFrame Stereo;
}

#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)
float I_3DSStereoStrength();
#endif
