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

#include "v_video.h"
#include "m_png.h"
#include "templates.h"
#include "r_videoscale.h"
#include "i_time.h"
#include "v_text.h"
#include "i_video.h"
#include "v_draw.h"

#include <cstring>

#include "hw_clock.h"
#include "hw_vrmodes.h"
#include "hw_cvars.h"
#include "hw_skydome.h"
#include "hwrenderer/data/hw_viewpointbuffer.h"
#include "flatvertices.h"
#include "hwrenderer/data/shaderuniforms.h"
#include "hw_lightbuffer.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"

#include "poly_framebuffer.h"
#include "poly_buffers.h"
#include "poly_renderstate.h"
#include "poly_hwtexture.h"
#include "engineerrors.h"

#ifdef __3DS__
#include "common/platform/3ds/diagnostics_3ds.h"
#include "common/platform/3ds/memory_3ds.h"
#endif

void Draw2D(F2DDrawer *drawer, FRenderState &state);

extern int rendered_commandbuffers;
extern int current_rendered_commandbuffers;

extern bool gpuStatActive;
extern bool keepGpuStatActive;
extern FString gpuStatOutput;

PolyFrameBuffer::PolyFrameBuffer(void *hMonitor, bool fullscreen) : Super(hMonitor, fullscreen) 
{
	I_PolyPresentInit();
}

PolyFrameBuffer::~PolyFrameBuffer()
{
	// screen is already null at this point, but PolyHardwareTexture::ResetAll needs it during clean up. Is there a better way we can do this?
	auto tmp = screen;
	screen = this;

	PolyHardwareTexture::ResetAll();
	PolyBuffer::ResetAll();
	PPResource::ResetAll();

	delete mScreenQuad.VertexBuffer;
	delete mScreenQuad.IndexBuffer;

	delete mVertexData;
	delete mSkyData;
	delete mViewpoints;
	delete mLights;
	mShadowMap.Reset();

	screen = tmp;

	I_PolyPresentDeinit();
}

void PolyFrameBuffer::InitializeState()
{
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-state-enter");
	#endif
	vendorstring = "Poly";
	hwcaps = RFL_SHADER_STORAGE_BUFFER | RFL_BUFFER_STORAGE;
	glslversion = 4.50f;
	uniformblockalignment = 1;
	maxuniformblock = 0x7fffffff;

	mRenderState.reset(new PolyRenderState());
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-render-state-ready");
	// Release the contiguous reserve immediately before vertex allocation.
	I_3DSReleaseRendererMemory();
	I_3DSStartupLog("softpoly-reserve-released");
	#endif

	mVertexData = new FFlatVertexBuffer(GetWidth(), GetHeight());
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-vertices-ready");
	#endif
	mSkyData = new FSkyVertexBuffer;
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-sky-ready");
	#endif
	mViewpoints = new HWViewpointBuffer;
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-viewpoints-ready");
	#endif
	mLights = new FLightBuffer();
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-lights-ready");
	#endif

	static const FVertexBufferAttribute format[] =
	{
		{ 0, VATTR_VERTEX, VFmt_Float3, (int)myoffsetof(ScreenQuadVertex, x) },
		{ 0, VATTR_TEXCOORD, VFmt_Float2, (int)myoffsetof(ScreenQuadVertex, u) },
		{ 0, VATTR_COLOR, VFmt_Byte4, (int)myoffsetof(ScreenQuadVertex, color0) }
	};

	uint32_t indices[6] = { 0, 1, 2, 1, 3, 2 };

	mScreenQuad.VertexBuffer = screen->CreateVertexBuffer();
	mScreenQuad.VertexBuffer->SetFormat(1, 3, sizeof(ScreenQuadVertex), format);

	mScreenQuad.IndexBuffer = screen->CreateIndexBuffer();
	mScreenQuad.IndexBuffer->SetData(6 * sizeof(uint32_t), indices, false);
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-screen-quad-ready");
	#endif

	CheckCanvas();
	#ifdef __3DS__
	I_3DSStartupLog("softpoly-canvas-ready");
	#endif
}

void PolyFrameBuffer::CheckCanvas()
{
	if (!mCanvas || mCanvas->GetWidth() != GetWidth() || mCanvas->GetHeight() != GetHeight())
	{
		FlushDrawCommands();
		DrawerThreads::WaitForWorkers();

		mCanvas.reset(new DCanvas(0, 0, true));
		mCanvas->Resize(GetWidth(), GetHeight(), false);
		mDepthStencil.reset();
		mDepthStencil.reset(new PolyDepthStencil(GetWidth(), GetHeight()));

		mRenderState->SetRenderTarget(GetCanvas(), GetDepthStencil(), true);
	}
}

PolyCommandBuffer *PolyFrameBuffer::GetDrawCommands()
{
	if (!mDrawCommands)
	{
		mDrawCommands.reset(new PolyCommandBuffer(&mFrameMemory));
		mDrawCommands->SetLightBuffer(mLightBuffer->Memory());
	}
	return mDrawCommands.get();
}

void PolyFrameBuffer::FlushDrawCommands()
{
	mRenderState->EndRenderPass();
	if (mDrawCommands)
	{
		mDrawCommands->Submit();
		mDrawCommands.reset();
	}
}

EXTERN_CVAR(Float, vid_brightness)
EXTERN_CVAR(Float, vid_contrast)
EXTERN_CVAR(Float, vid_saturation)

void PolyFrameBuffer::Update()
{
	twoD.Reset();
	Flush3D.Reset();

	Flush3D.Clock();

	Draw2D();
	twod->Clear();

	Flush3D.Unclock();

	FlushDrawCommands();

	if (mCanvas)
	{
		int w = mCanvas->GetWidth();
		int h = mCanvas->GetHeight();
		int pixelsize = 4;
		#ifdef __3DS__
		// Retire workers before composing the native HUD.
		DrawerThreads::WaitForWorkers();
		I_3DSComposeGameplayFrame(
			static_cast<unsigned char *>(mCanvas->GetPixels()),
			mCanvas->GetPitch() * pixelsize, w, h);
		if (!mNativeMenuBase.empty() && I_3DSNativeMenuVisible() &&
			mNativeMenuBaseWidth == w && mNativeMenuBaseHeight == h &&
			mNativeMenuBasePitch == mCanvas->GetPitch() * pixelsize)
		{
			I_3DSRouteNativeMenuFrame(
				static_cast<unsigned char *>(mCanvas->GetPixels()),
				mNativeMenuBase.data(), mNativeMenuBasePitch, w, h);
		}
		mNativeMenuBase.clear();
		mNativeMenuBasePitch = 0;
		mNativeMenuBaseWidth = 0;
		mNativeMenuBaseHeight = 0;
		#endif
		const uint8_t *src = (const uint8_t*)mCanvas->GetPixels();
		const bool neutralColorTransform = vid_gamma == 1.0f &&
			vid_contrast == 1.0f && vid_brightness == 0.0f &&
			vid_saturation == 1.0f;
		bool presented = false;
		#if defined(__3DS__) && (!defined(LOD3DS_SAFE_SOFTWARE) || defined(LOD3DS_HYBRID_PERFORMANCE))
		if (neutralColorTransform)
		{
			presented = I_PolyPresentDirect3DS(src, mCanvas->GetPitch() * pixelsize,
				w, h, mOutputLetterbox.left, mOutputLetterbox.top,
				mOutputLetterbox.width, mOutputLetterbox.height);
		}
		#endif
		if (!presented)
		{
			int pitch = 0;
			uint8_t *dst = I_PolyPresentLock(w, h, cur_vsync, pitch);
			if (dst)
			{
				#if defined(__3DS__) && !defined(LOD3DS_SAFE_SOFTWARE)
				if (neutralColorTransform)
				{
				const size_t rowBytes = static_cast<size_t>(w) * pixelsize;
				const size_t sourcePitchBytes =
					static_cast<size_t>(mCanvas->GetPitch()) * pixelsize;
				if (static_cast<size_t>(pitch) == rowBytes && sourcePitchBytes == rowBytes)
				{
					std::memcpy(dst, src, rowBytes * static_cast<size_t>(h));
				}
				else
				{
					for (int row = 0; row < h; ++row)
					{
						std::memcpy(dst + static_cast<size_t>(row) * pitch,
							src + static_cast<size_t>(row) * sourcePitchBytes, rowBytes);
					}
				}
				}
				else
				#endif
				{
					auto copyqueue = std::make_shared<DrawerCommandQueue>(&mFrameMemory);
					if (neutralColorTransform)
					{
						copyqueue->Push<MemcpyCommand>(dst, pitch / pixelsize, src, w, h,
							mCanvas->GetPitch(), pixelsize);
					}
					else
					{
						copyqueue->Push<CopyAndApplyGammaCommand>(dst, pitch / pixelsize, src, w, h,
							mCanvas->GetPitch(), vid_gamma, vid_contrast, vid_brightness, vid_saturation);
					}
					DrawerThreads::Execute(copyqueue);
					DrawerThreads::WaitForWorkers();
				}
				#if defined(__3DS__) && defined(LOD3DS_SAFE_SOFTWARE)
				static unsigned startupFrame = 0;
				static const char *const frameEnter[] = {
					"softpoly-frame-1-enter", "softpoly-frame-2-enter",
					"softpoly-frame-3-enter"
				};
				static const char *const framePresentReady[] = {
					"softpoly-frame-1-present-ready", "softpoly-frame-2-present-ready",
					"softpoly-frame-3-present-ready"
				};
				if (startupFrame < 3) I_3DSStartupLog(frameEnter[startupFrame]);
				#endif
				I_PolyPresentUnlock(mOutputLetterbox.left, mOutputLetterbox.top,
					mOutputLetterbox.width, mOutputLetterbox.height);
				#if defined(__3DS__) && defined(LOD3DS_SAFE_SOFTWARE)
				if (startupFrame < 3)
				{
					I_3DSStartupLog(framePresentReady[startupFrame]);
					++startupFrame;
				}
				#endif
			}
		}
		FPSLimit();
	}

	DrawerThreads::WaitForWorkers();
	mFrameMemory.Clear();
	FrameDeleteList.Buffers.clear();
	FrameDeleteList.Images.clear();

	CheckCanvas();

	Super::Update();
}


void PolyFrameBuffer::RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc)
{
	auto BaseLayer = static_cast<PolyHardwareTexture*>(tex->GetHardwareTexture(0, 0));

	DCanvas *image = BaseLayer->GetImage(tex, 0, 0);
	PolyDepthStencil *depthStencil = BaseLayer->GetDepthStencil(tex);
	mRenderState->SetRenderTarget(image, depthStencil, false);

	IntRect bounds;
	bounds.left = bounds.top = 0;
	bounds.width = std::min(tex->GetWidth(), image->GetWidth());
	bounds.height = std::min(tex->GetHeight(), image->GetHeight());

	renderFunc(bounds);

	FlushDrawCommands();
	DrawerThreads::WaitForWorkers();
	mRenderState->SetRenderTarget(GetCanvas(), GetDepthStencil(), true);

	tex->SetUpdated(true);
}

static uint8_t ToIntColorComponent(float v)
{
	return clamp((int)(v * 255.0f + 0.5f), 0, 255);
}

void PolyFrameBuffer::PostProcessScene(bool swscene, int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D)
{
	afterBloomDrawEndScene2D();

	if (fixedcm >= CM_FIRSTSPECIALCOLORMAP && fixedcm < CM_MAXCOLORMAP)
	{
		FSpecialColormap* scm = &SpecialColormaps[fixedcm - CM_FIRSTSPECIALCOLORMAP];

		mRenderState->SetViewport(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);
		screen->mViewpoints->Set2D(*mRenderState, screen->GetWidth(), screen->GetHeight());

		ScreenQuadVertex vertices[4] =
		{
			{ 0.0f, 0.0f, 0.0f, 0.0f },
			{ (float)mScreenViewport.width, 0.0f, 1.0f, 0.0f },
			{ 0.0f, (float)mScreenViewport.height, 0.0f, 1.0f },
			{ (float)mScreenViewport.width, (float)mScreenViewport.height, 1.0f, 1.0f }
		};
		mScreenQuad.VertexBuffer->SetData(4 * sizeof(ScreenQuadVertex), vertices, false);

		mRenderState->SetVertexBuffer(mScreenQuad.VertexBuffer, 0, 0);
		mRenderState->SetIndexBuffer(mScreenQuad.IndexBuffer);

		mRenderState->SetObjectColor(PalEntry(255, int(scm->ColorizeStart[0] * 127.5f), int(scm->ColorizeStart[1] * 127.5f), int(scm->ColorizeStart[2] * 127.5f)));
		mRenderState->SetAddColor(PalEntry(255, int(scm->ColorizeEnd[0] * 127.5f), int(scm->ColorizeEnd[1] * 127.5f), int(scm->ColorizeEnd[2] * 127.5f)));

		mRenderState->EnableDepthTest(false);
		mRenderState->EnableMultisampling(false);
		mRenderState->SetCulling(Cull_None);

		mRenderState->SetScissor(-1, -1, -1, -1);
		mRenderState->SetColor(1, 1, 1, 1);
		mRenderState->AlphaFunc(Alpha_GEqual, 0.f);
		mRenderState->EnableTexture(false);
		mRenderState->SetColormapShader(true);
		mRenderState->DrawIndexed(DT_Triangles, 0, 6);
		mRenderState->SetColormapShader(false);
		mRenderState->SetObjectColor(0xffffffff);
		mRenderState->SetAddColor(0);
		mRenderState->SetVertexBuffer(screen->mVertexData);
		mRenderState->EnableTexture(true);
		mRenderState->ResetColor();
	}
}


void PolyFrameBuffer::SetVSync(bool vsync)
{
	cur_vsync = vsync;
}

FRenderState* PolyFrameBuffer::RenderState()
{ 
	return mRenderState.get(); 
}


void PolyFrameBuffer::PrecacheMaterial(FMaterial *mat, int translation)
{
	if (mat->Source()->GetUseType() == ETextureType::SWCanvas) return;

	MaterialLayerInfo* layer;
	auto systex = static_cast<PolyHardwareTexture*>(mat->GetLayer(0, translation, &layer));
	systex->GetImage(layer->layerTexture, translation, layer->scaleFlags);

	int numLayers = mat->NumLayers();
	for (int i = 1; i < numLayers; i++)
	{
		auto systex = static_cast<PolyHardwareTexture*>(mat->GetLayer(i, 0, &layer));
		systex->GetImage(layer->layerTexture, 0, layer->scaleFlags);	// fixme: Upscale flags must be disabled for certain layers.
	}
}

IHardwareTexture *PolyFrameBuffer::CreateHardwareTexture(int numchannels)
{
	return new PolyHardwareTexture();
}

IVertexBuffer *PolyFrameBuffer::CreateVertexBuffer()
{
	return new PolyVertexBuffer();
}

IIndexBuffer *PolyFrameBuffer::CreateIndexBuffer()
{
	return new PolyIndexBuffer();
}

IDataBuffer *PolyFrameBuffer::CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize)
{
	IDataBuffer *buffer = new PolyDataBuffer(bindingpoint, ssbo, needsresize);
	if (bindingpoint == LIGHTBUF_BINDINGPOINT)
		mLightBuffer = buffer;
	return buffer;
}

void PolyFrameBuffer::SetTextureFilterMode()
{
}

void PolyFrameBuffer::BlurScene(float amount)
{
}

void PolyFrameBuffer::UpdatePalette()
{
}

FTexture *PolyFrameBuffer::WipeStartScreen()
{
	SetViewportRects(nullptr);

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<PolyHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeStartScreen");

	return tex;
}

FTexture *PolyFrameBuffer::WipeEndScreen()
{
	Draw2D();
	twod->Clear();

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<PolyHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeEndScreen");

	return tex;
}

TArray<uint8_t> PolyFrameBuffer::GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma)
{
	// [GEC] Really necessary to apply gamma, brightness, contrast and saturation for screenshot

	std::vector<uint8_t> gammatablebuf(256);
	uint8_t* gammatable = gammatablebuf.data();

	float InvGamma = 1.0f / clamp<float>(vid_gamma, 0.1f, 4.f);
	float Brightness = clamp<float>(vid_brightness, -0.8f, 0.8f);
	float Contrast = clamp<float>(vid_contrast, 0.1f, 3.f);
	float Saturation = clamp<float>(vid_saturation, -15.0f, 15.f);

	for (int x = 0; x < 256; x++)
	{
		float ramp = (float)(x / 255.f);
		// Apply Contrast
		// vec4 finalColor = vec4((((originalColor.rgb - vec3(0.5)) * Contrast) + vec3(0.5)), 1.0);
		if(vid_contrast != 1.0f)
			ramp = (((ramp - 0.5f) * Contrast) + 0.5f);

		// Apply Brightness
		// vec4 finalColor = vec4(originalColor.rgb + Brightness, 1.0);
		if (vid_brightness != 0.0f)
			ramp += (Brightness / 2.0f);

		// Apply Gamma
		// FragColor.rgb = pow(fragColor.rgb, vec3(1.0/gamma));
		if (vid_gamma != 1.0f)
			ramp = pow(ramp, InvGamma);

		// Clamp ramp
		ramp = clamp<float>(ramp, 0.0f, 1.f);

		gammatable[x] = (uint8_t)(ramp * 255);
	}

	int w = SCREENWIDTH;
	int h = SCREENHEIGHT;

	TArray<uint8_t> ScreenshotBuffer(w * h * 3, true);
	const uint8_t* pixels = GetCanvas()->GetPixels();
	int dindex = 0;

	// Convert to RGB
	for (int y = 0; y < h; y++)
	{
		int sindex = y * w * 4;

		for (int x = 0; x < w; x++)
		{
			uint32_t red = pixels[sindex + 2];
			uint32_t green = pixels[sindex + 1];
			uint32_t blue = pixels[sindex];

			if (vid_saturation != 1.0f)
			{
				float NewR = (float)(red / 255.f);
				float NewG = (float)(green / 255.f);
				float NewB = (float)(blue / 255.f);

				// Apply Saturation
				// float luma = dot(In, float3(0.2126729, 0.7151522, 0.0721750));
				// Out =  luma.xxx + Saturation.xxx * (In - luma.xxx);
				//float luma = (NewR * 0.2126729f) + (NewG * 0.7151522f) + (NewB * 0.0721750f); // Rec. 709
				float luma = (NewR * 0.299f) + (NewG * 0.587f) + (NewB * 0.114f); //Rec. 601
				NewR = luma + (Saturation * (NewR - luma));
				NewG = luma + (Saturation * (NewG - luma));
				NewB = luma + (Saturation * (NewB - luma));

				// Clamp All
				NewR = clamp<float>(NewR, 0.0f, 1.f);
				NewG = clamp<float>(NewG, 0.0f, 1.f);
				NewB = clamp<float>(NewB, 0.0f, 1.f);

				red = (uint32_t)(NewR * 255.f);
				green = (uint32_t)(NewG * 255.f);
				blue = (uint32_t)(NewB * 255.f);
			}

			// Apply Contrast / Brightness / Gamma
			red = gammatable[red];
			green = gammatable[green];
			blue = gammatable[blue];

			ScreenshotBuffer[dindex    ] = red;
			ScreenshotBuffer[dindex + 1] = green;
			ScreenshotBuffer[dindex + 2] = blue;

			dindex += 3;
			sindex += 4;
		}
	}

	pitch = w * 3;
	color_type = SS_RGB;
	gamma = 1.0f;
	return ScreenshotBuffer;
}

void PolyFrameBuffer::BeginFrame()
{
	#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)
	// DFrameBuffer normally applies a requested virtual-size change at the end
	// of the frame. Applying it before viewport setup prevents the first New
	// Game frame from being rendered with the title menu's old 400x240 canvas.
	Super::Update();
	#endif
	SetViewportRects(nullptr);
	CheckCanvas();

	#ifdef __3DS__
	// The Poly renderer's Clear(CT_Color) path is intentionally a no-op. On a
	// desktop compositor that is usually hidden by complete redraws, but the
	// 3DS scanout preserves uncovered pixels and leaked the previous HUD/frame
	// into sky columns. Start each software frame from transparent black: it
	// appears black at scanout and alpha 0 remains a sentinel for untouched
	// pixels that the software sky fallback fills later in the frame.
	if (mCanvas != nullptr && mCanvas->GetPixels() != nullptr)
	{
		constexpr size_t CanvasBytesPerPixel = 4;
		std::memset(mCanvas->GetPixels(), 0,
			static_cast<size_t>(mCanvas->GetPitch()) * mCanvas->GetHeight() *
				CanvasBytesPerPixel);
	}
	#endif

#if 0
	swrenderer::R_InitFuzzTable(GetCanvas()->GetPitch());
	static int next_random = 0;
	swrenderer::fuzzpos = (swrenderer::fuzzpos + swrenderer::fuzz_random_x_offset[next_random] * FUZZTABLE / 100) % FUZZTABLE;
	next_random++;
	if (next_random == FUZZ_RANDOM_X_SIZE)
		next_random = 0;
#endif
}

void PolyFrameBuffer::Draw2D()
{
	::Draw2D(twod, *mRenderState);
}

#ifdef __3DS__
void PolyFrameBuffer::CaptureNativeMenuBase()
{
	if (mCanvas == nullptr || mCanvas->GetPixels() == nullptr) return;

	// Resolve the world/page, blend and HUD that were queued before M_Drawer.
	// M_Drawer can then use the ordinary engine canvas without any special-case
	// rendering code, while this exact pre-menu image remains available for the
	// upper LCD restoration and menu-pixel extraction.
	Draw2D();
	twod->Clear();
	FlushDrawCommands();
	DrawerThreads::WaitForWorkers();

	mNativeMenuBasePitch = mCanvas->GetPitch() * 4;
	mNativeMenuBaseWidth = mCanvas->GetWidth();
	mNativeMenuBaseHeight = mCanvas->GetHeight();
	mNativeMenuBase.resize(static_cast<size_t>(mNativeMenuBasePitch) *
		mNativeMenuBaseHeight);
	std::memcpy(mNativeMenuBase.data(), mCanvas->GetPixels(),
		mNativeMenuBase.size());
}
#endif

unsigned int PolyFrameBuffer::GetLightBufferBlockSize() const
{
	return mLights->GetBlockSize();
}

void PolyFrameBuffer::UpdateShadowMap()
{
}

void PolyFrameBuffer::AmbientOccludeScene(float m5)
{
	//mPostprocess->AmbientOccludeScene(m5);
}
