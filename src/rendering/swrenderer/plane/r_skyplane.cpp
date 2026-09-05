//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2016 Magnus Norddahl
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//

#include <stdlib.h>
#include <float.h>
#include "templates.h"

#include "filesystem.h"
#include "doomdef.h"
#include "doomstat.h"
#include "r_sky.h"
#include "stats.h"
#include "v_video.h"
#include "a_sharedglobal.h"
#include "c_console.h"
#include "cmdlib.h"
#include "d_net.h"
#include "g_level.h"
#include "texturemanager.h"
#include "swrenderer/scene/r_opaque_pass.h"
#include "r_skyplane.h"
#include "swrenderer/scene/r_3dfloors.h"
#include "v_palette.h"
#include "r_data/colormaps.h"
#include "swrenderer/drawers/r_draw_rgba.h"
#include "a_dynlight.h"
#include "swrenderer/segments/r_clipsegment.h"
#include "swrenderer/segments/r_drawsegment.h"
#include "swrenderer/line/r_wallsetup.h"
#include "swrenderer/line/r_walldraw.h"
#include "swrenderer/scene/r_portal.h"
#include "swrenderer/scene/r_scene.h"
#include "swrenderer/scene/r_light.h"
#include "swrenderer/viewport/r_viewport.h"
#include "r_memory.h"
#include "swrenderer/r_renderthread.h"
#include "g_levellocals.h"

#ifdef __3DS__
#include "common/platform/3ds/diagnostics_3ds.h"
#endif

CVAR(Bool, r_linearsky, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
EXTERN_CVAR(Int, r_skymode)
EXTERN_CVAR(Bool, cl_oldfreelooklimit)

namespace swrenderer
{
	#ifdef __3DS__
	bool IsMap01ExteriorSkyLevel(RenderThread *thread)
	{
		auto viewport = thread != nullptr ? thread->Viewport.get() : nullptr;
		auto level = viewport != nullptr ? viewport->Level() : nullptr;
		auto sector = viewport != nullptr ? viewport->viewpoint.sector : nullptr;
		return level != nullptr && level->MapName.CompareNoCase("MAP01") == 0 &&
			sector != nullptr &&
			(sector->GetTexture(sector_t::ceiling) == skyflatnum ||
				sector->ValidatePortal(sector_t::ceiling) != nullptr) &&
			!viewport->RenderingToCanvas && viewport->RenderTarget != nullptr &&
			viewport->RenderTarget->IsBgra();
	}
	#endif

	static FSoftwareTexture *GetSWTex(FTextureID texid, bool allownull = true)
	{
		return GetPalettedSWTexture(texid, true, false, true);
	}

	RenderSkyPlane::RenderSkyPlane(RenderThread *thread)
	{
		Thread = thread;
		auto Level = Thread->Viewport->Level();

		auto sskytex1 = GetPalettedSWTexture(Level->skytexture1, true, false, true);
		auto sskytex2 = GetPalettedSWTexture(Level->skytexture2, true, false, true);

		if (sskytex1 == nullptr || sskytex2 == nullptr)
			return;

		skytexturemid = 0;
		int skyheight = sskytex1->GetScaledHeight();
		skyoffset = cl_oldfreelooklimit? 0 : skyheight == 256? 166 : skyheight >= 240? 150 : skyheight >= 200? 110 : 138;
		if (skyheight >= 128 && skyheight < 200)
		{
			skytexturemid = -28;
		}
		else if (skyheight >= 200)
		{
			skytexturemid = (200 - skyheight) * sskytex1->GetScale().Y + ((r_skymode == 2 && !(Level->flags & LEVEL_FORCETILEDSKY)) ? sskytex1->GetSkyOffset() : 0);
		}

		if (viewwidth != 0 && viewheight != 0)
		{
			skyiscale = float(r_Yaspect / freelookviewheight);
			skyscale = freelookviewheight / r_Yaspect;

			skyiscale *= float(thread->Viewport->viewpoint.FieldOfView.Degrees / 90.);
			skyscale *= float(90. / thread->Viewport->viewpoint.FieldOfView.Degrees);
		}

		if (Level->skystretch)
		{
			skyscale *= (double)(SKYSTRETCH_HEIGHT + skyoffset) / skyheight;
			skyiscale *= skyheight / (float)(SKYSTRETCH_HEIGHT + skyoffset);
			skytexturemid *= skyheight / (double)(SKYSTRETCH_HEIGHT + skyoffset);
		}

		// The standard Doom sky texture is 256 pixels wide, repeated 4 times over 360 degrees,
		// giving a total sky width of 1024 pixels. So if the sky texture is no wider than 1024,
		// we map it to a cylinder with circumfrence 1024. For larger ones, we use the width of
		// the texture as the cylinder's circumfrence.
		sky1cyl = MAX(sskytex1->GetWidth(), fixed_t(sskytex1->GetScale().X * 1024));
		sky2cyl = MAX(sskytex2->GetWidth(), fixed_t(sskytex2->GetScale().Y * 1024));
	}

	void RenderSkyPlane::Render(VisiblePlane *pl)
	{
		FTextureID sky1tex, sky2tex;
		double frontdpos = 0, backdpos = 0;
		auto Level = Thread->Viewport->Level();

		if ((Level->flags & LEVEL_SWAPSKIES) && !(Level->flags & LEVEL_DOUBLESKY))
		{
			sky1tex = Level->skytexture2;
		}
		else
		{
			sky1tex = Level->skytexture1;
		}
		sky2tex = Level->skytexture2;
		skymid = skytexturemid;
		skyangle = Thread->Viewport->viewpoint.Angles.Yaw.BAMs();

		if (pl->picnum == skyflatnum)
		{
			if (!(pl->sky & PL_SKYFLAT))
			{	// use sky1
			sky1:
				frontskytex = GetSWTex(sky1tex);
				if (Level->flags & LEVEL_DOUBLESKY)
					backskytex = GetSWTex(sky2tex);
				else
					backskytex = NULL;
				skyflip = 0;
				frontdpos = Level->sky1pos;
				backdpos = Level->sky2pos;
				frontcyl = sky1cyl;
				backcyl = sky2cyl;
			}
			else if (pl->sky == PL_SKYFLAT)
			{	// use sky2
				frontskytex = GetSWTex(sky2tex);
				backskytex = NULL;
				frontcyl = sky2cyl;
				skyflip = 0;
				frontdpos = Level->sky2pos;
			}
			else
			{	// MBF's linedef-controlled skies
				// Sky Linedef
				const line_t *l = &Level->lines[(pl->sky & ~PL_SKYFLAT) - 1];

				// Sky transferred from first sidedef
				const side_t *s = l->sidedef[0];
				int pos;

				// Texture comes from upper texture of reference sidedef
				// [RH] If swapping skies, then use the lower sidedef
				if (Level->flags & LEVEL_SWAPSKIES && s->GetTexture(side_t::bottom).isValid())
				{
					pos = side_t::bottom;
				}
				else
				{
					pos = side_t::top;
				}

				frontskytex = GetSWTex(s->GetTexture(pos));
				if (frontskytex == nullptr)
				{ // [RH] The blank texture: Use normal sky instead.
					goto sky1;
				}
				backskytex = NULL;

				// Horizontal offset is turned into an angle offset,
				// to allow sky rotation as well as careful positioning.
				// However, the offset is scaled very small, so that it
				// allows a long-period of sky rotation.
				skyangle += FLOAT2FIXED(s->GetTextureXOffset(pos));

				// Vertical offset allows careful sky positioning.
				skymid = s->GetTextureYOffset(pos) - 28.0;

				// We sometimes flip the picture horizontally.
				//
				// Doom always flipped the picture, so we make it optional,
				// to make it easier to use the new feature, while to still
				// allow old sky textures to be used.
				skyflip = l->args[2] ? 0u : ~0u;

				int frontxscale = int(frontskytex->GetScale().X * 1024);
				frontcyl = MAX(frontskytex->GetWidth(), frontxscale);
				if (Level->skystretch)
				{
					skymid = skymid * frontskytex->GetScaledHeight() / (SKYSTRETCH_HEIGHT + skyoffset);
				}
			}
		}
		frontpos = int(fmod(frontdpos, sky1cyl * 65536.0));
		if (backskytex != NULL)
		{
			backpos = int(fmod(backdpos, sky2cyl * 65536.0));
		}

		drawerargs.SetStyle();

		DrawSky(pl);
	}

	static uint32_t UMulScale16(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 16); }

	void RenderSkyPlane::DrawSkyColumnStripe(int start_x, int y1, int y2, double scale, double texturemid, double yrepeat)
	{
		RenderPortal *renderportal = Thread->Portal.get();
		auto viewport = Thread->Viewport.get();
		auto Level = viewport->Level();

		double uv_stepd = skyiscale * yrepeat;
		double v = (texturemid + uv_stepd * (y1 - viewport->CenterY + 0.5)) / frontskytex->GetHeight();
		double v_step = uv_stepd / frontskytex->GetHeight();

		uint32_t uv_pos = (uint32_t)(int32_t)(v * 0x01000000);
		uint32_t uv_step = (uint32_t)(int32_t)(v_step * 0x01000000);

		int x = start_x;
		if (renderportal->MirrorFlags & RF_XFLIP)
			x = (viewwidth - x);

		uint32_t ang, angle1, angle2;

		if (r_linearsky)
		{
			angle_t xangle = (angle_t)((0.5 - x / (double)viewwidth) * viewport->viewwindow.FocalTangent * ANGLE_90);
			ang = (skyangle + xangle) ^ skyflip;
		}
		else
		{
			ang = (skyangle + viewport->xtoviewangle[x]) ^ skyflip;
		}
		angle1 = UMulScale16(ang, frontcyl) + frontpos;
		angle2 = UMulScale16(ang, backcyl) + backpos;

		auto skycapcolors = Thread->GetSkyCapColor(frontskytex);

		drawerargs.SetFrontTexture(Thread, frontskytex, angle1);
		drawerargs.SetBackTexture(Thread, backskytex, angle2);
		drawerargs.SetTextureVStep(uv_step);
		drawerargs.SetTextureVPos(uv_pos);
		drawerargs.SetDest(viewport, start_x, y1);
		drawerargs.SetCount(y2 - y1);
		drawerargs.SetFadeSky(r_skymode == 2 && !(Level->flags & LEVEL_FORCETILEDSKY));
		drawerargs.SetSolidTop(skycapcolors.first);
		drawerargs.SetSolidBottom(skycapcolors.second);

		if (!backskytex)
			drawerargs.DrawSingleSkyColumn(Thread);
		else
			drawerargs.DrawDoubleSkyColumn(Thread);

		if (r_modelscene)
			drawerargs.DrawDepthSkyColumn(Thread, 1.0f / 65536.0f);
	}

	void RenderSkyPlane::DrawSkyColumn(int start_x, int y1, int y2)
	{
		if (1 << frontskytex->GetHeightBits() >= frontskytex->GetPhysicalHeight())
		{
			double texturemid = skymid * frontskytex->GetScale().Y + frontskytex->GetHeight();
			DrawSkyColumnStripe(start_x, y1, y2, frontskytex->GetScale().Y, texturemid, frontskytex->GetScale().Y);
		}
		else
		{
			auto viewport = Thread->Viewport.get();
			double yrepeat = frontskytex->GetScale().Y;
			double scale = frontskytex->GetScale().Y * skyscale;
			double iscale = 1 / scale;
			short drawheight = short(frontskytex->GetHeight() * scale);
			double topfrac = fmod(skymid + iscale * (1 - viewport->CenterY), frontskytex->GetHeight());
			if (topfrac < 0) topfrac += frontskytex->GetHeight();
			double texturemid = topfrac - iscale * (1 - viewport->CenterY);
			DrawSkyColumnStripe(start_x, y1, y2, scale, texturemid, yrepeat);
		}
	}

	void RenderSkyPlane::DrawSky(VisiblePlane *pl)
	{
		int x1 = pl->left;
		int x2 = pl->right;
		short *uwal = (short *)pl->top;
		short *dwal = (short *)pl->bottom;

		for (int x = x1; x < x2; x++)
		{
			int y1 = uwal[x];
			int y2 = dwal[x];
			if (y2 <= y1)
				continue;

			DrawSkyColumn(x, y1, y2);
		}
	}

	#ifdef __3DS__
	void RenderSkyPlane::FillPrimarySkyBackground()
	{
		// Do not hang this repair off an individual sky visplane. MAP01's portal
		// arrangement can produce only the small visible wedge and consequently
		// never satisfied the v0.2 visplane guard (confirmed by calls=0 in the
		// hardware dump). Install the level's primary sky explicitly after the
		// ordinary plane pass; later portal and translucent passes still overwrite
		// their own windows.
		auto viewport = Thread->Viewport.get();
		if (viewport == nullptr || viewport->Level() == nullptr) return;
		auto Level = viewport->Level();
		const FTextureID sky1tex =
			((Level->flags & LEVEL_SWAPSKIES) && !(Level->flags & LEVEL_DOUBLESKY)) ?
			Level->skytexture2 : Level->skytexture1;

		frontskytex = GetSWTex(sky1tex);
		if (frontskytex == nullptr) return;
		backskytex = (Level->flags & LEVEL_DOUBLESKY) ?
			GetSWTex(Level->skytexture2) : nullptr;
		skymid = skytexturemid;
		skyangle = viewport->viewpoint.Angles.Yaw.BAMs();
		skyflip = 0;
		frontcyl = MAX(frontskytex->GetWidth(),
			fixed_t(frontskytex->GetScale().X * 1024));
		backcyl = backskytex != nullptr ? MAX(backskytex->GetWidth(),
			fixed_t(backskytex->GetScale().X * 1024)) : frontcyl;
		if (frontcyl <= 0 || (backskytex != nullptr && backcyl <= 0)) return;
		frontpos = int(fmod(Level->sky1pos, frontcyl * 65536.0));
		backpos = backskytex != nullptr ?
			int(fmod(Level->sky2pos, backcyl * 65536.0)) : 0;
		drawerargs.SetStyle();
		FillTransparentSkyBackground();
	}

	void RenderSkyPlane::FillTransparentSkyBackground()
	{
		// The hardware dumps prove that Legend of Doom's hardware-oriented sky
		// layout can leave disjoint sky-visplane gaps in the classic software
		// renderer. Those pixels retain the canvas clear value (including alpha
		// 0), while walls, flats and deliberately black textures are opaque.
		// Project the active sky into only untouched runs.
		// This acts as the background a sky scene expects without painting over
		// any world geometry, HUD pixel, or legitimate opaque black surface.
		auto viewport = Thread->Viewport.get();
		if (Thread->SkyBackgroundFilled ||
			Thread->Portal->CurrentPortalUniq != 0 ||
			Thread->Clip3D->CurrentSkybox != 0 || viewport == nullptr ||
			viewport->RenderingToCanvas || viewport->RenderTarget == nullptr ||
			!viewport->RenderTarget->IsBgra())
		{
			return;
		}
		Thread->SkyBackgroundFilled = true;

		const int pitch = viewport->RenderTarget->GetPitch();
		const int sliceWidth = Thread->X2 - Thread->X1;
		unsigned int filledPixels = 0;
		if (sliceWidth <= 0 || viewheight <= 0)
		{
			I_3DSRecordSkyFallback(filledPixels);
			return;
		}

		// The canvas is row-major. The old x/y traversal jumped one full pitch for
		// every alpha test, turning the 320x200 sentinel scan into almost pure cache
		// misses on ARM11. Track each column's open transparent run while walking
		// contiguous BGRA pixels; emitting a run still uses the original sky-column
		// routine, so texture projection and the alpha==0 contract stay unchanged.
		int *transparentStarts =
			Thread->FrameMemory->AllocMemory<int>(sliceWidth);
		for (int column = 0; column < sliceWidth; ++column)
		{
			transparentStarts[column] = -1;
		}

		uint8_t *rowAlpha = viewport->GetDest(Thread->X1, 0) + 3;
		const size_t rowStride = static_cast<size_t>(pitch) * 4;
		const bool map01Fog = IsMap01ExteriorSkyLevel(Thread);
		const int fogHorizon = map01Fog ?
			clamp(xs_RoundToInt(viewport->CenterY), 0, viewheight) : viewheight;
		unsigned int foggedPixels = 0;
		for (int y = 0; y < viewheight; ++y, rowAlpha += rowStride)
		{
			uint8_t *alpha = rowAlpha;
			for (int column = 0; column < sliceWidth; ++column, alpha += 4)
			{
				int &first = transparentStarts[column];
				if (*alpha == 0)
				{
					if (first < 0) first = y;
				}
				else if (first >= 0)
				{
					filledPixels += static_cast<unsigned int>(y - first);
					const int skyEnd = MIN(y, MAX(first, fogHorizon));
					if (first < skyEnd)
						DrawSkyColumn(Thread->X1 + column, first, skyEnd);
					if (skyEnd < y)
						foggedPixels += FillMap01DistanceFogColumn(
							Thread->X1 + column, skyEnd, y);
					first = -1;
				}
			}
		}

		for (int column = 0; column < sliceWidth; ++column)
		{
			const int first = transparentStarts[column];
			if (first >= 0)
			{
				filledPixels += static_cast<unsigned int>(viewheight - first);
				const int skyEnd = MIN(viewheight, MAX(first, fogHorizon));
				if (first < skyEnd)
					DrawSkyColumn(Thread->X1 + column, first, skyEnd);
				if (skyEnd < viewheight)
					foggedPixels += FillMap01DistanceFogColumn(
						Thread->X1 + column, skyEnd, viewheight);
			}
		}
		RepairIsolatedTopSkyColumns();
		I_3DSRecordSkyFallback(filledPixels);
		I_3DSRecordDrawDistanceFog(foggedPixels);
	}

	unsigned int RenderSkyPlane::FillMap01DistanceFogColumn(int x, int y1, int y2)
	{
		if (!IsMap01ExteriorSkyLevel(Thread) || y2 <= y1) return 0;
		auto viewport = Thread->Viewport.get();
		const int pitch = viewport->RenderTarget->GetPitch();
		uint32_t *dest = reinterpret_cast<uint32_t *>(viewport->GetDest(x, y1));
		for (int y = y1; y < y2; ++y, dest += pitch)
		{
			// Match the exact terminal colour used by the explicit 1536..2048 world
			// fog. v0.17's unrelated vertical gradient exposed water, floor and door
			// cutoffs as flat blue rectangles even after the geometry was rejected.
			*dest = MAKEARGB(255, Map01DistanceFogRed,
				Map01DistanceFogGreen, Map01DistanceFogBlue);
		}
		return static_cast<unsigned int>(y2 - y1);
	}

	static unsigned int BgraRgbDistance(const uint8_t *a, const uint8_t *b)
	{
		const unsigned int blue = a[0] > b[0] ? a[0] - b[0] : b[0] - a[0];
		const unsigned int green = a[1] > b[1] ? a[1] - b[1] : b[1] - a[1];
		const unsigned int red = a[2] > b[2] ? a[2] - b[2] : b[2] - a[2];
		return blue + green + red;
	}

	static unsigned int BgraRgbMaxChannelDistance(const uint8_t *a,
		const uint8_t *b)
	{
		const unsigned int blue = a[0] > b[0] ? a[0] - b[0] : b[0] - a[0];
		const unsigned int green = a[1] > b[1] ? a[1] - b[1] : b[1] - a[1];
		const unsigned int red = a[2] > b[2] ? a[2] - b[2] : b[2] - a[2];
		return MAX(blue, MAX(green, red));
	}

	void RenderSkyPlane::RepairIsolatedTopSkyColumns()
	{
		// full-02's raw DCanvas proves the intermittent bar is already present
		// before PICA presentation: one opaque OBRKB wall column crosses otherwise
		// continuous sky. At 240x150 a far wall can collapse to a single sample.
		// Repair only a top-connected, one-column discontinuity whose two opaque
		// neighbours agree throughout a bounded probe. Legitimate wider geometry,
		// translucent objects, portal views and ordinary sky variation are left
		// untouched.
		auto viewport = Thread->Viewport.get();
		constexpr int ProbeRows = 8;
		constexpr unsigned int NeighbourMaxDistance = 16;
		constexpr unsigned int IntrusionMinDistance = 96;
		unsigned int repairedColumns = 0;
		unsigned int repairedPixels = 0;
		if (viewport == nullptr || viewheight < ProbeRows || viewwidth < 3)
		{
			I_3DSRecordSkyColumnRepair(repairedColumns, repairedPixels);
			return;
		}

		const int pitch = viewport->RenderTarget->GetPitch();
		const size_t rowStride = static_cast<size_t>(pitch) * 4;
		const int firstColumn = MAX(Thread->X1, 1);
		const int lastColumn = MIN(Thread->X2, viewwidth - 1);
		const int skyRepairBottom = IsMap01ExteriorSkyLevel(Thread) ?
			clamp(xs_RoundToInt(viewport->CenterY), 0, viewheight) : viewheight;
		for (int x = firstColumn; x < lastColumn; ++x)
		{
			uint8_t *center = viewport->GetDest(x, 0);
			uint8_t *left = center - 4;
			uint8_t *right = center + 4;
			bool isolated = true;
			for (int y = 0; y < ProbeRows; ++y)
			{
				if (center[3] == 0 || left[3] == 0 || right[3] == 0 ||
					BgraRgbMaxChannelDistance(left, right) > NeighbourMaxDistance ||
					BgraRgbDistance(center, left) < IntrusionMinDistance ||
					BgraRgbDistance(center, right) < IntrusionMinDistance)
				{
					isolated = false;
					break;
				}
				center += rowStride;
				left += rowStride;
				right += rowStride;
			}
			if (!isolated) continue;

			int end = ProbeRows;
			center = viewport->GetDest(x, end);
			left = center - 4;
			right = center + 4;
			while (end < skyRepairBottom && center[3] != 0 && left[3] != 0 &&
				right[3] != 0 &&
				BgraRgbDistance(center, left) >= IntrusionMinDistance &&
				BgraRgbDistance(center, right) >= IntrusionMinDistance)
			{
				++end;
				center += rowStride;
				left += rowStride;
				right += rowStride;
			}

			DrawSkyColumn(x, 0, end);
			++repairedColumns;
			repairedPixels += static_cast<unsigned int>(end);
		}
		I_3DSRecordSkyColumnRepair(repairedColumns, repairedPixels);
	}
	#endif
}
