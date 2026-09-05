#ifndef __I_VIDEO_H__
#define __I_VIDEO_H__

#include <cstdint>

class DFrameBuffer;


class IVideo
{
public:
	virtual ~IVideo() {}

	virtual DFrameBuffer *CreateFrameBuffer() = 0;

	bool SetResolution();

	virtual void DumpAdapters();
};

void I_InitGraphics();
void I_ShutdownGraphics();

extern IVideo *Video;

void I_PolyPresentInit();
uint8_t *I_PolyPresentLock(int w, int h, bool vsync, int &pitch);
void I_PolyPresentUnlock(int x, int y, int w, int h);
#ifdef __3DS__
// Presents the software canvas through the selected 3DS fast path. The hybrid
// profile uploads one bounded 240x150 texture and lets PICA200 scale it; other
// layouts return false so the complete SDL fallback remains available.
bool I_PolyPresentDirect3DS(const uint8_t *pixels, int pitch, int width,
	int height, int x, int y, int outputWidth, int outputHeight);
#endif
void I_PolyPresentDeinit();


// Pause a bit.
// [RH] Despite the name, it apparently never waited for the VBL, even in
// the original DOS version (if the Heretic/Hexen source is any indicator).
void I_WaitVBL(int count);


#endif // __I_VIDEO_H__
