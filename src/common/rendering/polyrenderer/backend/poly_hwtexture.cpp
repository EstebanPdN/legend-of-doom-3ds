/*
**  Softpoly backend
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "templates.h"
#include "c_cvars.h"
#include "hw_material.h"
#include "hw_cvars.h"
#include "hw_renderstate.h"
#include "poly_framebuffer.h"
#include "poly_hwtexture.h"

PolyHardwareTexture *PolyHardwareTexture::First = nullptr;

PolyHardwareTexture::PolyHardwareTexture()
{
	Next = First;
	First = this;
	if (Next) Next->Prev = this;
}

PolyHardwareTexture::~PolyHardwareTexture()
{
	if (Next) Next->Prev = Prev;
	if (Prev) Prev->Next = Next;
	else First = Next;

	Reset();
}

void PolyHardwareTexture::ResetAll()
{
	for (PolyHardwareTexture *cur = PolyHardwareTexture::First; cur; cur = cur->Next)
		cur->Reset();
}

void PolyHardwareTexture::Reset()
{
	if (auto fb = GetPolyFrameBuffer())
	{
		auto &deleteList = fb->FrameDeleteList;
		if (mCanvas) deleteList.Images.push_back(std::move(mCanvas));
	}
}

DCanvas *PolyHardwareTexture::GetImage(FTexture *tex, int translation, int flags)
{
	if (!mCanvas)
		CreateImage(tex, translation, flags);
	return mCanvas.get();
}

PolyDepthStencil *PolyHardwareTexture::GetDepthStencil(FTexture *tex)
{
	if (!mDepthStencil)
	{
		int w = tex->GetWidth();
		int h = tex->GetHeight();
		mDepthStencil.reset(new PolyDepthStencil(w, h));
	}
	return mDepthStencil.get();
}

void PolyHardwareTexture::AllocateBuffer(int w, int h, int texelsize)
{
	if (!mCanvas || mCanvas->GetWidth() != w || mCanvas->GetHeight() != h)
	{
		mCanvas.reset(new DCanvas(0, 0, texelsize == 4));
		mCanvas->Resize(w, h, false);
		bufferpitch = mCanvas->GetPitch();
	}
}

uint8_t *PolyHardwareTexture::MapBuffer()
{
	return mCanvas->GetPixels();
}

unsigned int PolyHardwareTexture::CreateTexture(unsigned char * buffer, int w, int h, int texunit, bool mipmap, const char *name)
{
	return 0;
}

void PolyHardwareTexture::CreateWipeTexture(int w, int h, const char *name)
{
	if (!mCanvas || mCanvas->GetWidth() != w || mCanvas->GetHeight() != h)
	{
		mCanvas.reset(new DCanvas(0, 0, true));
		mCanvas->Resize(w, h, false);
	}

	auto fb = static_cast<PolyFrameBuffer*>(screen);

	fb->FlushDrawCommands();
	DrawerThreads::WaitForWorkers();

	uint32_t* dest = (uint32_t*)mCanvas->GetPixels();
	const uint32_t* src = (const uint32_t*)fb->GetCanvas()->GetPixels();
	const int dpitch = mCanvas->GetPitch();
	const int spitch = fb->GetCanvas()->GetPitch();
	const int sourceWidth = fb->GetCanvas()->GetWidth();
	const int sourceHeight = fb->GetCanvas()->GetHeight();
	const int pixelsize = 4;

	if (sourceWidth == w && sourceHeight == h)
	{
		for (int y = 0; y < h; y++)
		{
			memcpy(dest + dpitch * (h - 1 - y), src + spitch * y,
				w * pixelsize);
		}
		return;
	}

	// A 3DS title-to-level transition changes the logical canvas from native
	// 400x240 to the 320x192 gameplay surface before the old frame is captured.
	// The old implementation copied only w*h pixels from the larger canvas,
	// turning the first wipe frame into a visibly enlarged top-left crop. Scale
	// the complete old frame into the new wipe texture instead. The fixed-point
	// bilinear sample runs only when a transition crosses canvas sizes.
	for (int y = 0; y < h; ++y)
	{
		int sourceY = ((2 * y + 1) * sourceHeight * 128) / h - 128;
		int y0 = sourceY >> 8;
		int yfrac = sourceY & 255;
		if (y0 < 0)
		{
			y0 = 0;
			yfrac = 0;
		}
		int y1 = y0 + 1;
		if (y1 >= sourceHeight)
		{
			y0 = sourceHeight - 1;
			y1 = y0;
			yfrac = 0;
		}

		uint8_t *destRow = reinterpret_cast<uint8_t *>(
			dest + dpitch * (h - 1 - y));
		const uint8_t *sourceRow0 = reinterpret_cast<const uint8_t *>(
			src + spitch * y0);
		const uint8_t *sourceRow1 = reinterpret_cast<const uint8_t *>(
			src + spitch * y1);
		for (int x = 0; x < w; ++x)
		{
			int sourceX = ((2 * x + 1) * sourceWidth * 128) / w - 128;
			int x0 = sourceX >> 8;
			int xfrac = sourceX & 255;
			if (x0 < 0)
			{
				x0 = 0;
				xfrac = 0;
			}
			int x1 = x0 + 1;
			if (x1 >= sourceWidth)
			{
				x0 = sourceWidth - 1;
				x1 = x0;
				xfrac = 0;
			}

			const uint8_t *p00 = sourceRow0 + x0 * pixelsize;
			const uint8_t *p10 = sourceRow0 + x1 * pixelsize;
			const uint8_t *p01 = sourceRow1 + x0 * pixelsize;
			const uint8_t *p11 = sourceRow1 + x1 * pixelsize;
			uint8_t *out = destRow + x * pixelsize;
			for (int channel = 0; channel < pixelsize; ++channel)
			{
				const unsigned top = p00[channel] * (256 - xfrac) +
					p10[channel] * xfrac;
				const unsigned bottom = p01[channel] * (256 - xfrac) +
					p11[channel] * xfrac;
				out[channel] = static_cast<uint8_t>((top * (256 - yfrac) +
					bottom * yfrac + 32768) >> 16);
			}
		}
	}
}

void PolyHardwareTexture::CreateImage(FTexture *tex, int translation, int flags)
{
	mCanvas.reset(new DCanvas(0, 0, true));

	if (!tex->isHardwareCanvas())
	{
		FTextureBuffer texbuffer = tex->CreateTexBuffer(translation, flags | CTF_ProcessData);
		mCanvas->Resize(texbuffer.mWidth, texbuffer.mHeight, false);
		memcpy(mCanvas->GetPixels(), texbuffer.mBuffer, texbuffer.mWidth * texbuffer.mHeight * 4);
	}
	else
	{
		int w = tex->GetWidth();
		int h = tex->GetHeight();
		mCanvas->Resize(w, h, false);
	}
}
