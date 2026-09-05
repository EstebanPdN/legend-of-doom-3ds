/*
** sdlglvideo.cpp
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Christoph Oelckers et.al.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

// HEADER FILES ------------------------------------------------------------

#include "i_module.h"
#include "i_soundinternal.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "v_video.h"
#include "version.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "printf.h"

#include <cstring>

#include "hardware.h"
#include "gl_sysfb.h"
#ifndef __3DS__
#include "gl_system.h"
#endif

#ifndef __3DS__
#include "gl_renderer.h"
#include "gl_framebuffer.h"
#endif
#ifdef HAVE_GLES2
#include "gles_framebuffer.h"
#endif
#ifdef __3DS__
#include <3ds.h>
#ifdef LOD3DS_HYBRID_PERFORMANCE
#include <citro2d.h>
#include "common/platform/3ds/present_pixels.h"

extern "C"
{
	extern u32 __ctru_linear_heap;
	extern u32 __ctru_linear_heap_size;
}
#endif
#ifndef LOD3DS_SAFE_SOFTWARE
#include <NovaGL.h>
#endif
#include "common/platform/3ds/cache_3ds.h"
#include "common/platform/3ds/diagnostics_3ds.h"
#endif
 
#ifdef HAVE_VULKAN
#include "vulkan/system/vk_framebuffer.h"
#endif

#ifdef HAVE_SOFTPOLY
#include "poly_framebuffer.h"
#endif

#if defined(LOD3DS_SAFE_SOFTWARE) && !defined(HAVE_SOFTPOLY)
#error "LOD3DS_SAFE_SOFTWARE requires the CPU renderer (HAVE_SOFTPOLY)"
#endif

// MACROS ------------------------------------------------------------------

#if defined HAVE_VULKAN
#include <SDL_vulkan.h>
#endif // HAVE_VULKAN

// TYPES -------------------------------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------
extern IVideo *Video;

EXTERN_CVAR (Int, vid_adapter)
EXTERN_CVAR (Int, vid_displaybits)
EXTERN_CVAR (Int, vid_defwidth)
EXTERN_CVAR (Int, vid_defheight)
EXTERN_CVAR (Int, vid_preferbackend)
EXTERN_CVAR (Bool, cl_capfps)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Bool, gl_debug, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}
CUSTOM_CVAR(Bool, gl_es, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CVAR(Bool, i_soundinbackground, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CVAR (Int, vid_adapter, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(String, vid_sdl_render_driver, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CCMD(vid_list_sdl_render_drivers)
{
	for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i)
	{
		SDL_RendererInfo info;
		if (SDL_GetRenderDriverInfo(i, &info) == 0)
			Printf("%s\n", info.name);
	}
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace Priv
{
	SDL_Window *window;
	bool vulkanEnabled;
	bool softpolyEnabled;
	bool fullscreenSwitch;

	void CreateWindow(uint32_t extraFlags)
	{
		assert(Priv::window == nullptr);

		// Set default size
		SDL_Rect bounds;
		#ifdef __3DS__
		SDL_GetDisplayBounds(0, &bounds);
		#else
		SDL_GetDisplayBounds(vid_adapter, &bounds);
		#endif

#ifdef __3DS__
		win_w = 400;
		win_h = 240;
#else
		if (win_w <= 0 || win_h <= 0)
		{
			win_w = bounds.w * 8 / 10;
			win_h = bounds.h * 8 / 10;
		}
#endif

		FString caption;
		caption.Format(GAMENAME " %s (%s)", GetVersionString(), GetGitTime());

		const uint32_t windowFlags =
#ifdef __3DS__
			extraFlags;
#else
			(win_maximized ? SDL_WINDOW_MAXIMIZED : 0) | SDL_WINDOW_RESIZABLE | extraFlags;
#endif
		#ifdef __3DS__
		// SDL's N3DS backend derives the target LCD from the display encoded in
		// the window position. Never let archived desktop coordinates or a stale
		// vid_adapter route the recovery framebuffer to the bottom screen.
		const int windowX = SDL_WINDOWPOS_CENTERED_DISPLAY(0);
		const int windowY = SDL_WINDOWPOS_CENTERED_DISPLAY(0);
		#else
		const int windowX = (win_x <= 0) ? SDL_WINDOWPOS_CENTERED_DISPLAY(vid_adapter) : win_x;
		const int windowY = (win_y <= 0) ? SDL_WINDOWPOS_CENTERED_DISPLAY(vid_adapter) : win_y;
		#endif
		Priv::window = SDL_CreateWindow(caption,
			windowX, windowY,
			win_w, win_h, windowFlags);

		if (Priv::window != nullptr)
		{
#ifndef __3DS__
			// Enforce minimum size limit
			SDL_SetWindowMinimumSize(Priv::window, VID_MIN_WIDTH, VID_MIN_HEIGHT);
			// Tell SDL to start sending text input on Wayland.
			if (strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) == 0) SDL_StartTextInput();
#endif
		}
	}

	void DestroyWindow()
	{
		assert(Priv::window != nullptr);

		SDL_DestroyWindow(Priv::window);
		Priv::window = nullptr;
	}

	void SetupPixelFormat(int multisample, const int *glver)
	{
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		if (multisample > 0) {
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisample);
		}
		if (gl_debug)
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

		if (gl_es)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		}
		else if (glver[0] > 2)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glver[0]);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glver[1]);
		}
		else
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		}
	}
}

class SDLVideo : public IVideo
{
public:
	SDLVideo ();
	~SDLVideo ();

	DFrameBuffer *CreateFrameBuffer ();

private:
#ifdef HAVE_VULKAN
	VulkanDevice *device = nullptr;
#endif
};

// CODE --------------------------------------------------------------------

#ifdef HAVE_VULKAN
void I_GetVulkanDrawableSize(int *width, int *height)
{
	assert(Priv::vulkanEnabled);
	assert(Priv::window != nullptr);
	SDL_Vulkan_GetDrawableSize(Priv::window, width, height);
}

bool I_GetVulkanPlatformExtensions(unsigned int *count, const char **names)
{
	assert(Priv::vulkanEnabled);
	assert(Priv::window != nullptr);
	return SDL_Vulkan_GetInstanceExtensions(Priv::window, count, names) == SDL_TRUE;
}

bool I_CreateVulkanSurface(VkInstance instance, VkSurfaceKHR *surface)
{
	assert(Priv::vulkanEnabled);
	assert(Priv::window != nullptr);
	return SDL_Vulkan_CreateSurface(Priv::window, instance, surface) == SDL_TRUE;
}
#endif

#ifdef HAVE_SOFTPOLY
namespace
{
	SDL_Renderer* polyrendertarget = nullptr;
	SDL_Texture* polytexture = nullptr;
	int polytexturew = 0;
	int polytextureh = 0;
	bool polyvsync = false;
	bool polyfirstinit = true;

	#ifdef LOD3DS_HYBRID_PERFORMANCE
	constexpr int HybridMaximumCanvasWidth = 400;
	constexpr int HybridMaximumCanvasHeight = 240;
	constexpr int HybridTextureWidth = 512;
	constexpr int HybridTextureHeight = 256;
	C3D_RenderTarget *hybridTarget = nullptr;
	C3D_Tex hybridTexture{};
	Tex3DS_SubTexture hybridSubtexture{};
	uint32_t *hybridUpload = nullptr;
	void *hybridC2DFlushBase = nullptr;
	size_t hybridC2DFlushSize = 0;
	bool hybridC3DReady = false;
	bool hybridC2DReady = false;
	bool hybridPresenterReady = false;

	u32 HybridTextureTransferFlags()
	{
		return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
			GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
	}

	u32 HybridOutputTransferFlags()
	{
		return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
			GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
	}

	void ConfigureHybridBgraTexture()
	{
		// Map uploaded BGRA bytes to RGB through sampled G/B/A.
		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		C3D_TexEnvColor(env, C2D_Color32(255, 0, 0, 255));

		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(env, C2D_Color32(0, 255, 0, 255));

		env = C3D_GetTexEnv(2);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
	}

	void DeinitHybridPresenter()
	{
		if (hybridTarget != nullptr)
		{
			C3D_RenderTargetDelete(hybridTarget);
			hybridTarget = nullptr;
		}
		if (hybridTexture.data != nullptr)
		{
			C3D_TexDelete(&hybridTexture);
			hybridTexture = {};
		}
		if (hybridC2DReady)
		{
			C2D_Fini();
			hybridC2DReady = false;
		}
		if (hybridC3DReady)
		{
			C3D_Fini();
			hybridC3DReady = false;
		}
		if (hybridUpload != nullptr)
		{
			linearFree(hybridUpload);
			hybridUpload = nullptr;
		}
		hybridC2DFlushBase = nullptr;
		hybridC2DFlushSize = 0;
		hybridPresenterReady = false;
	}

	bool InitHybridPresenter()
	{
		const size_t uploadBytes = HybridTextureWidth * HybridTextureHeight * sizeof(uint32_t);
		hybridUpload = static_cast<uint32_t *>(linearMemAlign(uploadBytes, 0x80));
		if (hybridUpload == nullptr) return false;
		std::memset(hybridUpload, 0, uploadBytes);
		GSPGPU_FlushDataCache(hybridUpload, uploadBytes);

		if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
		{
			DeinitHybridPresenter();
			return false;
		}
		hybridC3DReady = true;
		if (!C2D_Init(16))
		{
			DeinitHybridPresenter();
			return false;
		}
		hybridC2DReady = true;
		C2D_Prepare();
		// Allow presentation up to the LCD refresh rate.
		C3D_FrameRate(60.0f);

		// Flush Citro2D vertices explicitly with GX_CMDLIST_FLUSH.
		C3D_BufInfo *buffers = C3D_GetBufInfo();
		if (buffers != nullptr && buffers->bufCount > 0)
		{
			const u32 heapPhysical = osConvertVirtToPhys(
				reinterpret_cast<void *>(__ctru_linear_heap));
			const u32 vertexPhysical = buffers->base_paddr + buffers->buffers[0].offset;
			const uintptr_t heapStart = static_cast<uintptr_t>(__ctru_linear_heap);
			const uintptr_t heapEnd = heapStart + __ctru_linear_heap_size;
			const uintptr_t vertexAddress = heapStart + (vertexPhysical - heapPhysical);
			const uintptr_t flushStart = vertexAddress & ~static_cast<uintptr_t>(0x7f);
			const uintptr_t flushEnd = std::min(flushStart + 64u * 1024u, heapEnd);
			if (flushStart >= heapStart && flushStart < flushEnd)
			{
				hybridC2DFlushBase = reinterpret_cast<void *>(flushStart);
				hybridC2DFlushSize = flushEnd - flushStart;
			}
		}
		if (hybridC2DFlushBase == nullptr)
		{
			DeinitHybridPresenter();
			return false;
		}

		if (!C3D_TexInitVRAM(&hybridTexture, HybridTextureWidth,
			HybridTextureHeight, GPU_RGBA8))
		{
			DeinitHybridPresenter();
			return false;
		}
		// One PICA200 bilinear sample performs the requested smooth upscale; the
		// software world renderer still shades only its selected internal pixels.
		C3D_TexSetFilter(&hybridTexture, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&hybridTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

		hybridTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
		if (hybridTarget == nullptr)
		{
			DeinitHybridPresenter();
			return false;
		}
		C3D_RenderTargetSetOutput(hybridTarget, GFX_TOP, GFX_LEFT,
			HybridOutputTransferFlags());
		hybridPresenterReady = true;
		return true;
	}
	#endif
}

void I_PolyPresentInit()
{
	assert(Priv::softpolyEnabled);
	assert(Priv::window != nullptr);

	#ifdef __3DS__
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	I_3DSStartupLog(InitHybridPresenter()
		? "hybrid-presenter-ready" : "hybrid-presenter-sdl-fallback");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	#endif
	// Nintendo 3DS has no accelerated SDL_Renderer. Select the documented
	// surface renderer explicitly. Ignore a stale desktop renderer setting:
	// the N3DS backend only implements the software presentation path.
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	#else
	if (strcmp(vid_sdl_render_driver, "") != 0)
	{
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, vid_sdl_render_driver);
	}
	#endif
}

uint8_t *I_PolyPresentLock(int w, int h, bool vsync, int &pitch)
{
	// When vsync changes we need to reinitialize
	if (polyrendertarget && polyvsync != vsync)
	{
		I_PolyPresentDeinit();
	}

	if (!polyrendertarget)
	{
		polyvsync = vsync;

		polyrendertarget = SDL_CreateRenderer(Priv::window, -1, vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
		if (!polyrendertarget)
		{
			I_FatalError("Could not create render target for softpoly: %s\n", SDL_GetError());
		}

		// Tell the user which render driver is being used, but don't repeat
		// outselves if we're just changing vsync.
		if (polyfirstinit)
		{
			polyfirstinit = false;

			SDL_RendererInfo rendererInfo;
			if (SDL_GetRendererInfo(polyrendertarget, &rendererInfo) == 0)
			{
				Printf("Using render driver %s\n", rendererInfo.name);
			}
			else
			{
				Printf("Failed to query render driver\n");
			}
		}

		// Mask color
		SDL_SetRenderDrawColor(polyrendertarget, 0, 0, 0, 255);
	}

	if (!polytexture || polytexturew != w || polytextureh != h)
	{
		if (polytexture)
		{
			SDL_DestroyTexture(polytexture);
			polytexture = nullptr;
			polytexturew = polytextureh = 0;
		}
		if ((polytexture = SDL_CreateTexture(polyrendertarget, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h)) == nullptr)
			I_Error("Failed to create %dx%d render target texture.", w, h);
		polytexturew = w;
		polytextureh = h;
	}

	uint8_t* pixels;
	SDL_LockTexture(polytexture, nullptr, (void**)&pixels, &pitch);
	return pixels;
}

void I_PolyPresentUnlock(int x, int y, int width, int height)
{
	SDL_UnlockTexture(polytexture);

	int ClientWidth, ClientHeight;
	SDL_GetRendererOutputSize(polyrendertarget, &ClientWidth, &ClientHeight);

	SDL_Rect clearrects[4];
	int count = 0;
	if (y > 0)
	{
		clearrects[count].x = 0;
		clearrects[count].y = 0;
		clearrects[count].w = ClientWidth;
		clearrects[count].h = y;
		count++;
	}
	if (y + height < ClientHeight)
	{
		clearrects[count].x = 0;
		clearrects[count].y = y + height;
		clearrects[count].w = ClientWidth;
		clearrects[count].h = ClientHeight - clearrects[count].y;
		count++;
	}
	if (x > 0)
	{
		clearrects[count].x = 0;
		clearrects[count].y = y;
		clearrects[count].w = x;
		clearrects[count].h = height;
		count++;
	}
	if (x + width < ClientWidth)
	{
		clearrects[count].x = x + width;
		clearrects[count].y = y;
		clearrects[count].w = ClientWidth - clearrects[count].x;
		clearrects[count].h = height;
		count++;
	}

	if (count > 0)
		SDL_RenderFillRects(polyrendertarget, clearrects, count);

	SDL_Rect dstrect;
	dstrect.x = x;
	dstrect.y = y;
	dstrect.w = width;
	dstrect.h = height;
	SDL_RenderCopy(polyrendertarget, polytexture, nullptr, &dstrect);

	SDL_RenderPresent(polyrendertarget);
	#if defined(__3DS__) && !defined(LOD3DS_SAFE_SOFTWARE)
	// SDL 2.32's N3DS software renderer advertises PRESENTVSYNC but its
	// framebuffer update only flushes and swaps. Pace after that single owner
	// of presentation so we neither run flat-out nor duplicate a swap/flush.
	gspWaitForVBlank();
	#endif
}

#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)
bool I_PolyPresentDirect3DS(const uint8_t *pixels, int pitch, int width,
	int height, int x, int y, int outputWidth, int outputHeight)
{
	if (!hybridPresenterReady || pixels == nullptr || width < 1 ||
		height < 1 || width > HybridMaximumCanvasWidth ||
		height > HybridMaximumCanvasHeight || pitch < width * 4 || x != 0 ||
		y != 0 || outputWidth != 400 || outputHeight != 240)
	{
		return false;
	}

	const size_t uploadBytes = lod3ds::CopyPresentPixels(hybridUpload,
		HybridTextureWidth, pixels, pitch, width, height);
	if (R_FAILED(GSPGPU_FlushDataCache(hybridUpload, uploadBytes))) return false;

	if (!C3D_FrameBegin(0)) return false;
	C3D_SyncDisplayTransfer(hybridUpload,
		GX_BUFFER_DIM(HybridTextureWidth, HybridTextureHeight),
		static_cast<u32 *>(hybridTexture.data),
		GX_BUFFER_DIM(HybridTextureWidth, HybridTextureHeight),
		HybridTextureTransferFlags());

	hybridSubtexture = Tex3DS_SubTexture{
		static_cast<u16>(width), static_cast<u16>(height),
		0.0f, 1.0f,
		static_cast<float>(width) / HybridTextureWidth,
		1.0f - static_cast<float>(height) / HybridTextureHeight
	};
	const C2D_Image image{ &hybridTexture, &hybridSubtexture };
	const C2D_DrawParams params{
		{ 0.0f, 0.0f, 400.0f, 240.0f },
		{ 0.0f, 0.0f }, 0.0f, 0.0f
	};
	C2D_TargetClear(hybridTarget, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(hybridTarget);
	// Set BGRA swizzle before Citro2D snapshots the draw state.
	ConfigureHybridBgraTexture();
	C2D_DrawImage(image, &params, nullptr);
	C2D_Flush();
	GSPGPU_FlushDataCache(hybridC2DFlushBase, hybridC2DFlushSize);
	C3D_FrameEnd(GX_CMDLIST_FLUSH);
	return true;
}
#elif defined(__3DS__) && !defined(LOD3DS_SAFE_SOFTWARE)
bool I_PolyPresentDirect3DS(const uint8_t *pixels, int pitch, int width,
	int height, int x, int y, int outputWidth, int outputHeight)
{
	// This is deliberately an exact-layout fast path. The generic SDL route
	// remains available for resolution, letterbox or colour-transform modes
	// whose mapping has not been validated on physical hardware.
	constexpr int SourceWidth = 320;
	constexpr int SourceHeight = 200;
	constexpr int ScreenWidth = 400;
	constexpr int ScreenHeight = 240;
	if (pixels == nullptr || width != SourceWidth || height != SourceHeight ||
		pitch < SourceWidth * 4 || (pitch & 3) != 0 || x != 0 || y != 0 ||
		outputWidth != ScreenWidth || outputHeight != ScreenHeight)
	{
		return false;
	}

	u16 physicalWidth = 0;
	u16 physicalHeight = 0;
	auto *framebuffer = reinterpret_cast<uint32_t *>(
		gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &physicalWidth, &physicalHeight));
	if (framebuffer == nullptr || physicalWidth != ScreenHeight ||
		physicalHeight != ScreenWidth)
	{
		return false;
	}

	// Both scales are rational and constant for this profile. Cache the maps so
	// the hot loop contains no division. Eight-row tiles keep the vertical
	// source samples and each physical LCD column resident together in L1.
	static uint16_t sourceX[ScreenWidth];
	static uint16_t sourceY[ScreenHeight];
	static bool mapsReady = false;
	if (!mapsReady)
	{
		// Match SDL 2.32's nearest-neighbour 16.16 accumulator exactly. A
		// rational centre formula chooses the other source texel at tie points,
		// which would make the direct and fallback presenters visibly differ.
		constexpr uint32_t SourceXStep =
			(static_cast<uint32_t>(SourceWidth) << 16) / ScreenWidth;
		constexpr uint32_t SourceYStep =
			(static_cast<uint32_t>(SourceHeight) << 16) / ScreenHeight;
		uint32_t sourceXPosition = SourceXStep / 2;
		for (int outX = 0; outX < ScreenWidth; ++outX)
		{
			sourceX[outX] = static_cast<uint16_t>(sourceXPosition >> 16);
			sourceXPosition += SourceXStep;
		}
		uint32_t sourceYPosition = SourceYStep / 2;
		for (int outY = 0; outY < ScreenHeight; ++outY)
		{
			sourceY[outY] = static_cast<uint16_t>(sourceYPosition >> 16);
			sourceYPosition += SourceYStep;
		}
		mapsReady = true;
	}

	const int sourcePitchWords = pitch / 4;
	const auto *source = reinterpret_cast<const uint32_t *>(pixels);
	constexpr int Tile = 8;
	for (int tileY = 0; tileY < ScreenHeight; tileY += Tile)
	{
		for (int tileX = 0; tileX < ScreenWidth; tileX += Tile)
		{
			for (int outX = tileX; outX < tileX + Tile; ++outX)
			{
				uint32_t *destinationColumn = framebuffer + outX * ScreenHeight;
				const int inX = sourceX[outX];
				for (int outY = tileY; outY < tileY + Tile; ++outY)
				{
					const uint32_t bgra = source[sourceY[outY] * sourcePitchWords + inX];
					// DCanvas stores B,G,R,A bytes (numeric AARRGGBB). The
					// libctru RGBA8 scanout stores A,B,G,R (numeric RRGGBBAA).
					destinationColumn[ScreenHeight - 1 - outY] =
						(bgra << 8) | (bgra >> 24);
				}
			}
		}
	}

	I_3DSCleanDataCache(framebuffer,
		static_cast<u32>(physicalWidth) * physicalHeight * sizeof(uint32_t));
	gfxScreenSwapBuffers(GFX_TOP, false);
	// SDL's N3DS software presenter performed this same single pacing wait.
	// Keeping it here avoids an unbounded producer and preserves HOME timing.
	gspWaitForVBlank();
	return true;
}
#endif

void I_PolyPresentDeinit()
{
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	DeinitHybridPresenter();
	#endif
	if (polytexture)
	{
		SDL_DestroyTexture(polytexture);
		polytexture = nullptr;
	}

	if (polyrendertarget)
	{
		SDL_DestroyRenderer(polyrendertarget);
		polyrendertarget = nullptr;
	}
}
#endif


SDLVideo::SDLVideo ()
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "Video initialization failed: %s\n", SDL_GetError());
		return;
	}

	// Fail gracefully if we somehow reach here after linking against a SDL2 library older than 2.0.6.
	SDL_version sdlver;
	SDL_GetVersion(&sdlver);
	if (!(sdlver.patch >= 6))
	{
		I_FatalError("Only SDL 2.0.6 or later is supported.");
	}

#ifdef HAVE_SOFTPOLY
	#ifdef LOD3DS_SAFE_SOFTWARE
	// This recovery profile must be immune to stale user CVARs and argument
	// ordering: it is not allowed to enter NovaGL/PICA on physical hardware.
	Priv::softpolyEnabled = true;
	#else
	Priv::softpolyEnabled = vid_preferbackend == 2;
	#endif
#endif
#ifdef HAVE_VULKAN
	Priv::vulkanEnabled = vid_preferbackend == 1;

	if (Priv::vulkanEnabled)
	{
		Priv::CreateWindow(SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | (vid_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));

		if (Priv::window == nullptr)
		{
			Priv::vulkanEnabled = false;
		}
	}
#endif
#ifdef HAVE_SOFTPOLY
	if (Priv::softpolyEnabled)
	{
		#ifdef __3DS__
		I_3DSStartupLog("safe-software-window-enter");
		#endif
		Priv::CreateWindow(SDL_WINDOW_HIDDEN);
		if (Priv::window == nullptr)
		{
			I_FatalError("Could not create SoftPoly window:\n%s\n",SDL_GetError());
		}
		#ifdef __3DS__
		I_3DSStartupLog("safe-software-window-ready");
		// Stop and join the early animation before SDL's software renderer starts
		// presenting game frames to the same top-screen scanout buffers.
		I_3DSLoadingScreenFinish();
		#endif
	}
#endif
}

SDLVideo::~SDLVideo ()
{
#ifdef HAVE_VULKAN
	delete device;
#endif
}

DFrameBuffer *SDLVideo::CreateFrameBuffer ()
{
	SystemBaseFrameBuffer *fb = nullptr;

	// first try Vulkan, if that fails OpenGL
#ifdef HAVE_VULKAN
	if (Priv::vulkanEnabled)
	{
		try
		{
			assert(device == nullptr);
			device = new VulkanDevice();
			fb = new VulkanFrameBuffer(nullptr, vid_fullscreen, device);
		}
		catch (CVulkanError const &error)
		{
			if (Priv::window != nullptr)
			{
				Priv::DestroyWindow();
			}

			Printf(TEXTCOLOR_RED "Initialization of Vulkan failed: %s\n", error.what());
			Priv::vulkanEnabled = false;
		}
	}
#endif

#ifdef HAVE_SOFTPOLY
	if (Priv::softpolyEnabled)
	{
		#ifdef __3DS__
		I_3DSStartupLog("safe-software-framebuffer-enter");
		#endif
		fb = new PolyFrameBuffer(nullptr, vid_fullscreen);
		#ifdef __3DS__
		I_3DSStartupLog("safe-software-framebuffer-ready");
		#endif
	}
#endif
	if (fb == nullptr)
	{
#if defined(__3DS__) && defined(LOD3DS_SAFE_SOFTWARE)
		I_FatalError("Hardware-safe build could not initialize the CPU framebuffer.");
#elif defined(__3DS__)
		fb = new OpenGLESRenderer::OpenGLFrameBuffer(0, vid_fullscreen);
#else
#ifdef HAVE_GLES2
		if( (Args->CheckParm ("-gles2_renderer")) || (vid_preferbackend == 3) )
			fb = new OpenGLESRenderer::OpenGLFrameBuffer(0, vid_fullscreen);
		else
#endif
			fb = new OpenGLRenderer::OpenGLFrameBuffer(0, vid_fullscreen);
#endif
	}

	return fb;
}


IVideo *gl_CreateVideo()
{
	return new SDLVideo();
}


// FrameBuffer Implementation -----------------------------------------------

SystemBaseFrameBuffer::SystemBaseFrameBuffer (void *, bool fullscreen)
: DFrameBuffer (vid_defwidth, vid_defheight)
{
	if (Priv::window != nullptr)
	{
		SDL_SetWindowFullscreen(Priv::window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		SDL_ShowWindow(Priv::window);
	}
}

int SystemBaseFrameBuffer::GetClientWidth()
{
	int width = 0;

#ifdef HAVE_SOFTPOLY
	if (Priv::softpolyEnabled)
	{
		if (polyrendertarget)
			SDL_GetRendererOutputSize(polyrendertarget, &width, nullptr);
		else
			SDL_GetWindowSize(Priv::window, &width, nullptr);
		return width;
	}
#endif
	
#ifdef HAVE_VULKAN
	assert(Priv::vulkanEnabled);
	SDL_Vulkan_GetDrawableSize(Priv::window, &width, nullptr);
#endif

	return width;
}

int SystemBaseFrameBuffer::GetClientHeight()
{
	int height = 0;

#ifdef HAVE_SOFTPOLY
	if (Priv::softpolyEnabled)
	{
		if (polyrendertarget)
			SDL_GetRendererOutputSize(polyrendertarget, nullptr, &height);
		else
			SDL_GetWindowSize(Priv::window, nullptr, &height);
		return height;
	}
#endif

#ifdef HAVE_VULKAN
	assert(Priv::vulkanEnabled);
	SDL_Vulkan_GetDrawableSize(Priv::window, nullptr, &height);
#endif

	return height;
}

bool SystemBaseFrameBuffer::IsFullscreen ()
{
	return (SDL_GetWindowFlags(Priv::window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void SystemBaseFrameBuffer::ToggleFullscreen(bool yes)
{
	SDL_SetWindowFullscreen(Priv::window, yes ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	if ( !yes )
	{
		if ( !Priv::fullscreenSwitch )
		{
			Priv::fullscreenSwitch = true;
			vid_fullscreen = false;
		}
		else
		{
			Priv::fullscreenSwitch = false;
			SetWindowSize(win_w, win_h);
		}
	}
}

void SystemBaseFrameBuffer::SetWindowSize(int w, int h)
{
	if (w < VID_MIN_WIDTH || h < VID_MIN_HEIGHT)
	{
		w = VID_MIN_WIDTH;
		h = VID_MIN_HEIGHT;
	}
	win_w = w;
	win_h = h;
	if (vid_fullscreen)
	{
		vid_fullscreen = false;
	}
	else
	{
		win_maximized = false;
		SDL_SetWindowSize(Priv::window, w, h);
		SDL_SetWindowPosition(Priv::window, SDL_WINDOWPOS_CENTERED_DISPLAY(vid_adapter), SDL_WINDOWPOS_CENTERED_DISPLAY(vid_adapter));
		SetSize(GetClientWidth(), GetClientHeight());
		int x, y;
		SDL_GetWindowPosition(Priv::window, &x, &y);
		win_x = x;
		win_y = y;
	}
}


SystemGLFrameBuffer::SystemGLFrameBuffer(void *hMonitor, bool fullscreen)
: SystemBaseFrameBuffer(hMonitor, fullscreen)
{
#ifdef __3DS__
	GLContext = nullptr;
	#ifdef LOD3DS_SAFE_SOFTWARE
	I_FatalError("Hardware-safe build rejected the NovaGL framebuffer path.");
	#else
	I_3DSStartupLog("sdl-window-enter");
	Priv::CreateWindow(0);
	if (Priv::window == nullptr)
	{
		I_FatalError("Could not create Nintendo 3DS video window:\n%s\n", SDL_GetError());
	}
	I_3DSStartupLog("sdl-window-ready");

	const u32 linearBefore = linearSpaceFree();
	const u32 vramBefore = vramSpaceFree();
	I_3DSStartupLog("novagl-enter");
	// Leave a completed progress screen scanned out until NovaGL presents the
	// first real frame. Unlike tearing gfx down and starting it again, this
	// avoids returning to a black LCD during the final renderer setup.
	I_3DSLoadingScreenFinish();
	// Keep one reusable frame slot. Counting submitted frames is not a GPU
	// completion fence: on real hardware the queue can still be consuming slot
	// N when the CPU wraps around and reuses it. NovaGL therefore drains the
	// pinned Citro3D render queue before reusing rings or deferred allocations,
	// and explicitly flushes every CPU-written GPU range and command-list split.
	// This replaces the old display-counter-only SYNCDRAW assumption that could
	// leave PICA running while video, input and HOME/APT appeared frozen.
	novaSetFrameBuffers(1);
	nova_init_ex(NOVA_CMD_BUF_SIZE, 2 * 1024 * 1024, 512 * 1024, 512 * 1024);
	novaSetSwapInterval(1);
	I_3DSStartupLog("novagl-ready");
	Printf("[NovaGL] init synchronized cmd=3M vertex=2M index=512K staging=512K; linear %u -> %u, VRAM %u -> %u bytes\n",
		(unsigned)linearBefore, (unsigned)linearSpaceFree(),
		(unsigned)vramBefore, (unsigned)vramSpaceFree());
	#endif
#else
	// NOTE: Core profiles were added with GL 3.2, so there's no sense trying
	// to set core 3.1 or 3.0. We could try a forward-compatible context
	// instead, but that would be too restrictive (w.r.t. shaders).
	static const int glvers[][2] = {
		{ 4, 6 }, { 4, 5 }, { 4, 4 }, { 4, 3 }, { 4, 2 }, { 4, 1 }, { 4, 0 },
		{ 3, 3 }, { 3, 2 }, { 2, 0 },
		{ 0, 0 },
	};
	int glveridx = 0;
	int i;

	const char *version = Args->CheckValue("-glversion");
	if (version != NULL)
	{
		double gl_version = strtod(version, NULL) + 0.01;
		int vermaj = (int)gl_version;
		int vermin = (int)(gl_version*10.0) % 10;

		while (glvers[glveridx][0] > vermaj || (glvers[glveridx][0] == vermaj &&
		        glvers[glveridx][1] > vermin))
		{
			glveridx++;
			if (glvers[glveridx][0] == 0)
			{
				glveridx = 0;
				break;
			}
		}
	}

	for ( ; glvers[glveridx][0] > 0; ++glveridx)
	{
		Priv::SetupPixelFormat(0, glvers[glveridx]);
		Priv::CreateWindow(SDL_WINDOW_OPENGL | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));

		if (Priv::window == nullptr)
		{
			continue;
		}

		GLContext = SDL_GL_CreateContext(Priv::window);
		if (GLContext == nullptr)
		{
			Priv::DestroyWindow();
		}
		else
		{
			break;
		}
	}
	if (Priv::window == nullptr)
	{
		I_FatalError("Could not create OpenGL window:\n%s\n",SDL_GetError());
	}
#endif
}

SystemGLFrameBuffer::~SystemGLFrameBuffer ()
{
	if (Priv::window)
	{
#ifdef __3DS__
		#ifndef LOD3DS_SAFE_SOFTWARE
		nova_fini();
		#endif
#else
		if (GLContext)
		{
			SDL_GL_DeleteContext(GLContext);
		}
#endif

		Priv::DestroyWindow();
	}
}

int SystemGLFrameBuffer::GetClientWidth()
{
#ifdef __3DS__
	return 400;
#else
	int width = 0;
	SDL_GL_GetDrawableSize(Priv::window, &width, nullptr);
	return width;
#endif
}

int SystemGLFrameBuffer::GetClientHeight()
{
#ifdef __3DS__
	return 240;
#else
	int height = 0;
	SDL_GL_GetDrawableSize(Priv::window, nullptr, &height);
	return height;
#endif
}

void SystemGLFrameBuffer::SetVSync( bool vsync )
{
#if defined (__3DS__)
	#ifdef LOD3DS_SAFE_SOFTWARE
	I_FatalError("Hardware-safe build rejected NovaGL VSync.");
	#else
	novaSetSwapInterval(vsync ? 1 : 0);
	#endif
#elif defined (__APPLE__)
	const GLint value = vsync ? 1 : 0;
	CGLSetParameter( CGLGetCurrentContext(), kCGLCPSwapInterval, &value );
#else
	if (vsync)
	{
		if (SDL_GL_SetSwapInterval(-1) == -1)
			SDL_GL_SetSwapInterval(1);
	}
	else
	{
		SDL_GL_SetSwapInterval(0);
	}
#endif
}

void SystemGLFrameBuffer::SwapBuffers()
{
#ifdef __3DS__
	#ifdef LOD3DS_SAFE_SOFTWARE
	I_FatalError("Hardware-safe build rejected a NovaGL buffer swap.");
	#else
	novaSwapBuffers();
	#endif
#else
	SDL_GL_SwapWindow(Priv::window);
#endif
}


void ProcessSDLWindowEvent(const SDL_WindowEvent &event)
{
	switch (event.event)
	{
	extern bool AppActive;

	case SDL_WINDOWEVENT_FOCUS_GAINED:
		S_SetSoundPaused(1);
		AppActive = true;
		break;

	case SDL_WINDOWEVENT_FOCUS_LOST:
		S_SetSoundPaused(i_soundinbackground);
		AppActive = false;
		break;

	case SDL_WINDOWEVENT_MOVED:
		if (!vid_fullscreen)
		{
			int top = 0, left = 0;
			SDL_GetWindowBordersSize(Priv::window, &top, &left, nullptr, nullptr);
			win_x = event.data1-left;
			win_y = event.data2-top;
		}
		break;

	case SDL_WINDOWEVENT_RESIZED:
		if (!vid_fullscreen && !Priv::fullscreenSwitch)
		{
			win_w = event.data1;
			win_h = event.data2;
		}
		break;

	case SDL_WINDOWEVENT_MAXIMIZED:
		win_maximized = true;
		break;

	case SDL_WINDOWEVENT_RESTORED:
		win_maximized = false;
		break;
	}
}


// each platform has its own specific version of this function.
void I_SetWindowTitle(const char* caption)
{
	if (caption)
	{
		SDL_SetWindowTitle(Priv::window, caption);
	}
	else
	{
		FString default_caption;
		default_caption.Format(GAMENAME " %s (%s)", GetVersionString(), GetGitTime());
		SDL_SetWindowTitle(Priv::window, default_caption);
	}
}
