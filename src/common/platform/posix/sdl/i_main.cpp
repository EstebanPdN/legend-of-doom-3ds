/*
** i_main.cpp
** System-specific startup code. Eventually calls D_DoomMain.
**
**---------------------------------------------------------------------------
** Copyright 1998-2007 Randy Heit
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

#include <SDL.h>
#include <unistd.h>
#include <signal.h>
#include <new>
#include <sys/param.h>
#include <locale.h>
#include <sys/stat.h>
#ifndef __3DS__
#include <sys/utsname.h>
#endif
#ifdef __3DS__
#include <3ds.h>
#include "common/platform/3ds/diagnostics_3ds.h"
#include "common/platform/3ds/lifecycle_3ds.h"
#include "common/platform/3ds/memory_3ds.h"

#ifndef LOD3DS_BUILD_ID
#define LOD3DS_BUILD_ID "untracked"
#endif

#ifndef LOD3DS_BUILD_PROFILE_NAME
#define LOD3DS_BUILD_PROFILE_NAME "release"
#endif

#ifndef LOD3DS_PORT_VERSION
#define LOD3DS_PORT_VERSION "dev"
#endif

extern "C"
{
	u32 __stacksize__ = 512 * 1024;
}
#endif

#include "engineerrors.h"
#include "m_argv.h"
#include "c_console.h"
#include "version.h"
#include "cmdlib.h"
#include "engineerrors.h"
#include "i_system.h"
#include "i_interface.h"
#include "printf.h"

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

extern "C" int cc_install_handlers(int, char**, int, int*, const char*, int(*)(char*, char*));

#ifdef __APPLE__
void Mac_I_FatalError(const char* errortext);
#endif

#ifdef __linux__
void Linux_I_FatalError(const char* errortext);
#endif

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------
int GameMain();

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// The command line arguments.
FArgs *Args;

// PRIVATE DATA DEFINITIONS ------------------------------------------------


// CODE --------------------------------------------------------------------



static int GetCrashInfo (char *buffer, char *end)
{
	if (sysCallbacks.CrashInfo) sysCallbacks.CrashInfo(buffer, end - buffer, "\n");
	return strlen(buffer);
}

void I_DetectOS()
{
#ifdef __3DS__
	Printf("OS: Nintendo 3DS\n");
	Printf("[lod3ds] version=%s build_id=%s profile=%s\n",
		LOD3DS_PORT_VERSION, LOD3DS_BUILD_ID, LOD3DS_BUILD_PROFILE_NAME);
	Printf("[lod3ds] memory app_free=%lu linear_free=%lu bytes\n",
		static_cast<unsigned long>(osGetMemRegionFree(MEMREGION_APPLICATION)),
		static_cast<unsigned long>(linearSpaceFree()));
	return;
#else
	FString operatingSystem;

	const char *paths[] = {"/etc/os-release", "/usr/lib/os-release"};

	for (const char *path : paths)
	{
		struct stat dummy;

		if (stat(path, &dummy) != 0)
			continue;

		char cmdline[256];
		snprintf(cmdline, sizeof cmdline, ". %s && echo ${PRETTY_NAME}", path);

		FILE *proc = popen(cmdline, "r");

		if (proc == nullptr)
			continue;

		char distribution[256] = {};
		fread(distribution, sizeof distribution - 1, 1, proc);

		const size_t length = strlen(distribution);

		if (length > 1)
		{
			distribution[length - 1] = '\0';
			operatingSystem = distribution;
		}

		pclose(proc);
		break;
	}

	utsname unameInfo;

	if (uname(&unameInfo) == 0)
	{
		const char* const separator = operatingSystem.Len() > 0 ? ", " : "";
		operatingSystem.AppendFormat("%s%s %s on %s", separator, unameInfo.sysname, unameInfo.release, unameInfo.machine);
	}

	if (operatingSystem.Len() > 0)
		Printf("OS: %s\n", operatingSystem.GetChars());
#endif
}

void I_StartupJoysticks();

int main (int argc, char **argv)
{
#if !defined (__APPLE__) && !defined(__3DS__)
	{
		int s[4] = { SIGSEGV, SIGILL, SIGFPE, SIGBUS };
		cc_install_handlers(argc, argv, 4, s, GAMENAMELOWERCASE "-crash.log", GetCrashInfo);
	}
#endif // !__APPLE__

	printf(GAMENAME" %s - %s - SDL version\nCompiled on %s\n",
		GetVersionString(), GetGitTime(), __DATE__);

#ifndef __3DS__
	seteuid (getuid ());
	// Set LC_NUMERIC environment variable in case some library decides to
	// clear the setlocale call at least this will be correct.
	// Note that the LANG environment variable is overridden by LC_*
	setenv ("LC_NUMERIC", "C", 1);
#else
	// SDL2 enables the New 3DS CPU/L2 speedup by default. Calling this here
	// makes the New 3DS-only performance policy explicit.
	osSetSpeedupEnable(true);
	I_3DSStartupLog("main-enter");
	#ifdef LOD3DS_SAFE_SOFTWARE
	I_3DSStartupLog(I_3DSReserveRendererMemory()
		? "renderer-reserve-ready" : "renderer-reserve-unavailable");
	#endif
	const bool exitSupervisorStarted = I_3DSStartExitSupervisor();
	I_3DSStartupLog(exitSupervisorStarted
		? "exit-supervisor-ready" : "exit-supervisor-unavailable");
	struct F3DSExitSupervisorScope
	{
		~F3DSExitSupervisorScope()
		{
			I_3DSStopExitSupervisor();
		}
	} exitSupervisorScope;
	printf("[lod3ds] version=%s build_id=%s profile=%s\n",
		LOD3DS_PORT_VERSION, LOD3DS_BUILD_ID, LOD3DS_BUILD_PROFILE_NAME);
#endif

	setlocale (LC_ALL, "C");

	if (SDL_Init (0) < 0)
	{
		fprintf (stderr, "Could not initialize SDL:\n%s\n", SDL_GetError());
		return -1;
	}
#ifdef __3DS__
	// Let SDL/libctru own and configure the CPU-writable RGBA8 scanout before
	// the animated startup screen touches it. This preserves the fix for Luma
	// dump 00000101, where a pre-SDL CPU write targeted protected VRAM.
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) >= 0)
	{
		I_3DSLoadingScreenStart();
	}
	I_3DSStartupLog("sdl-core-ready");
#endif

	printf("\n");
	
	Args = new FArgs(argc, argv);

#ifdef __3DS__
	const bool homebrewLaunch = envIsHomebrew();
	static const char *const SdDataDirectory = "sdmc:/3ds/legend-of-doom/data/";
	static const char *const RomFsDataDirectory = "romfs:/data/";
	static const char *const SdIWad = "sdmc:/3ds/legend-of-doom/data/freedoom2.wad";
	static const char *const RomFsIWad = "romfs:/data/freedoom2.wad";
	static const char *const SdMod = "sdmc:/3ds/legend-of-doom/data/LegendOfDoom.pk3";
	static const char *const RomFsMod = "romfs:/data/LegendOfDoom.pk3";
	static const char *const SdBase = "sdmc:/3ds/legend-of-doom/data/gzdoom.pk3";
	static const char *const RomFsBase = "romfs:/data/gzdoom.pk3";
	static const char *const SdSupport = "sdmc:/3ds/legend-of-doom/data/game_support.pk3";
	static const char *const RomFsSupport = "romfs:/data/game_support.pk3";
	const char *const DataDirectory = homebrewLaunch ? SdDataDirectory : RomFsDataDirectory;
	const char *const IWadPath = homebrewLaunch ? SdIWad : RomFsIWad;
	const char *const ModPath = homebrewLaunch ? SdMod : RomFsMod;
	const char *const BasePath = homebrewLaunch ? SdBase : RomFsBase;
	const char *const SupportPath = homebrewLaunch ? SdSupport : RomFsSupport;

	struct stat runtimeFile = {};
	const char *missingPath = nullptr;
	const char *const RuntimePaths[] = { BasePath, SupportPath, IWadPath, ModPath };
	for (const char *path : RuntimePaths)
	{
		if (stat(path, &runtimeFile) != 0)
		{
			missingPath = path;
			break;
		}
	}
	if (missingPath != nullptr)
	{
		char error[512];
		snprintf(error, sizeof(error),
			"Legend of Doom runtime data is missing for the %s launch: %s",
			homebrewLaunch ? "3DSX/SD" : "CIA/RomFS", missingPath);
		I_3DSWriteFatalLog(error);
		fprintf(stderr, "%s\n", error);
		return -1;
	}
	I_3DSStartupLog(homebrewLaunch ? "runtime-data-sd-ready" : "runtime-data-romfs-ready");

#ifdef LOD3DS_HARDWARE_DIAGNOSTIC
	// Capture NovaGL's bounded first-draw diagnostics without requiring a
	// debugger or spamming Azahar's UI. Release builds compile the probes out.
	if (freopen("sdmc:/3ds/legend-of-doom/novagl.log", "w", stdout) != nullptr)
	{
		setvbuf(stdout, nullptr, _IOLBF, 0);
	}
#endif

	Args->AppendArg("-iwad");
	Args->AppendArg(IWadPath);
	Args->AppendArg("-file");
	Args->AppendArg(ModPath);
	Args->AppendArg("-width");
	#ifdef LOD3DS_SAFE_SOFTWARE
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	Args->AppendArg("240");
	#else
	Args->AppendArg("320");
	#endif
	#else
	Args->AppendArg("400");
	#endif
	Args->AppendArg("-height");
	#ifdef LOD3DS_SAFE_SOFTWARE
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	Args->AppendArg("150");
	#else
	Args->AppendArg("200");
	#endif
	#else
	Args->AppendArg("240");
	#endif
	#ifdef LOD3DS_SAFE_SOFTWARE
	Args->AppendArg("+vid_adapter");
	Args->AppendArg("0");
	#endif
	Args->AppendArg("+vid_preferbackend");
	#ifdef LOD3DS_SAFE_SOFTWARE
	// SoftPoly remains the only world renderer in both safe profiles. The hybrid
	// build submits only a bounded presenter quad; NovaGL never sees geometry.
	Args->AppendArg("2");
	#else
	Args->AppendArg("3");
	#endif
	Args->AppendArg("+vid_rendermode");
	#ifdef LOD3DS_SAFE_SOFTWARE
	// True-colour software writes the BGRA canvas consumed by the direct scanout
	// path and avoids a separate per-frame palette conversion.
	Args->AppendArg("1");
	// Fix gameplay at 80% of the physical 400x240 LCD: 320x192 is roughly 30%
	// more detailed in each axis than the previous 240x150 profile. Menus switch
	// to the native canvas, but no touch control or frame cap changes the game.
	Args->AppendArg("+vid_scalemode");
	Args->AppendArg("5");
	Args->AppendArg("+vid_scale_customwidth");
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	Args->AppendArg("320");
	#else
	Args->AppendArg("320");
	#endif
	Args->AppendArg("+vid_scale_customheight");
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	Args->AppendArg("192");
	#else
	Args->AppendArg("200");
	#endif
	Args->AppendArg("+vid_scale_custompixelaspect");
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	Args->AppendArg("1.0");
	#else
	Args->AppendArg("0.96");
	#endif
	Args->AppendArg("+vid_scalefactor");
	Args->AppendArg("1");
	Args->AppendArg("+vid_cropaspect");
	Args->AppendArg("0");
	// The fused scanout presenter is exact only for neutral colour transforms.
	// Override stale desktop gamma settings so the normal hardware-safe launch
	// cannot silently fall back to three separate copy/scale/rotate passes.
	Args->AppendArg("+vid_gamma");
	Args->AppendArg("1.0");
	Args->AppendArg("+vid_contrast");
	Args->AppendArg("1.0");
	Args->AppendArg("+vid_brightness");
	Args->AppendArg("0.0");
	Args->AppendArg("+vid_saturation");
	Args->AppendArg("1.0");
	#else
	Args->AppendArg("4");
	#endif
	Args->AppendArg("+r_multithreaded");
	#ifdef LOD3DS_HYBRID_PERFORMANCE
	Args->AppendArg("2");
	#else
	Args->AppendArg("0");
	#endif
	Args->AppendArg("+r_scene_multithreaded");
	// Scene traversal retains its audited main-thread ownership. The two-way
	// split happens inside the drawer where every command is explicitly
	// scanline-partitioned and free of shared scene mutation.
	Args->AppendArg("0");
	// Preserve the user's master/music/effects sliders. The New 3DS-specific
	// final gain is applied once at the OpenAL listener, before its PCM limiter,
	// so looping a stream cannot accumulate or reset the boost.
	// Keep the original pixel-art presentation and avoid the 33% mip-chain
	// VRAM overhead on the PICA200 texture budget.
	Args->AppendArg("+gl_texture_filter");
	Args->AppendArg("0");
	Args->AppendArg("+gl_texture_filter_anisotropic");
	Args->AppendArg("1");
	// The classic true-colour software renderer has its own sampler controls;
	// gl_texture_filter does not affect them. Keep the pixel-art path nearest
	// and avoid per-column LOD calculations and four-tap filtering on ARM11.
	Args->AppendArg("+r_mipmap");
	Args->AppendArg("0");
	Args->AppendArg("+r_minfilter");
	Args->AppendArg("0");
	Args->AppendArg("+r_magfilter");
	Args->AppendArg("0");
	Args->AppendArg("+gl_texture_hqresizemode");
	Args->AppendArg("0");
	Args->AppendArg("+gl_precache");
	Args->AppendArg("0");
	// Opaque draw lists may be reordered safely. Grouping walls/flats by texture
	// cuts NovaGL texture binds and PICA command traffic without changing the
	// depth-tested result; translucent lists retain their order.
	Args->AppendArg("+gl_sort_textures");
	Args->AppendArg("1");
	// New 3DS baseline: spend CPU/GPU time on the world renderer, not desktop
	// post effects or dynamic-light preparation that NovaGL cannot accelerate.
	// These command-line values also override stale desktop settings on the SD.
	// Keep display synchronization to avoid tearing, but do not impose a software
	// frame cap. Rendering remains independent from the 35 Hz game simulation and
	// may reach the LCD's 60 Hz presentation rate when the scene is fast enough.
	Args->AppendArg("+vid_vsync");
	Args->AppendArg("1");
	Args->AppendArg("+vid_maxfps");
	Args->AppendArg("0");
	Args->AppendArg("+cl_capfps");
	Args->AppendArg("0");
	Args->AppendArg("+gl_multithread");
	// Cross-core execution of the upstream BSP queue is not race-free on ARM:
	// fake-sector allocation and several renderer flags remain shared. Compile
	// that queue out on 3DS and override stale archived configs in every profile.
	Args->AppendArg("0");
	Args->AppendArg("+gl_lights");
	Args->AppendArg("0");
	// gl_lights only controls the hardware renderer.  The software and Poly
	// renderers use r_dynlights instead, so leaving it at the desktop default
	// kept building per-surface light lists in the supposedly low-cost profile.
	Args->AppendArg("+r_dynlights");
	Args->AppendArg("0");
	// MAP01's original isometric layout contains dense connected geometry more
	// than ten thousand map units away. Exterior geometry fades from 1536 to
	// 2048 units and is rejected only after reaching the horizon colour. Mixed
	// and interior BSP subtrees remain intact so doors and enclosed floors do
	// not acquire holes merely because the player is standing outdoors.
	Args->AppendArg("+r_line_distance_cull");
	Args->AppendArg("2048");
	Args->AppendArg("+r_sprite_distance_cull");
	Args->AppendArg("2048");
	Args->AppendArg("+gl_light_sprites");
	Args->AppendArg("0");
	Args->AppendArg("+gl_light_particles");
	Args->AppendArg("0");
	Args->AppendArg("+gl_plane_reflection");
	Args->AppendArg("0");
	Args->AppendArg("+gl_mirror_envmap");
	Args->AppendArg("0");
	Args->AppendArg("+gl_fxaa");
	Args->AppendArg("0");
	Args->AppendArg("+gl_ssao");
	Args->AppendArg("0");
	Args->AppendArg("+autoloadlights");
	Args->AppendArg("0");
	Args->AppendArg("+autoloadbrightmaps");
	Args->AppendArg("0");
	Args->AppendArg("+autoloadwidescreen");
	Args->AppendArg("0");
	Args->AppendArg("+r_drawvoxels");
	Args->AppendArg("0");
	// Preserve gameplay while bounding secondary visual work. Actor shadows
	// duplicate sprite draws, and this mod can otherwise reserve/spawn four
	// thousand particles on a CPU/GPU budget intended for a 400x240 display.
	Args->AppendArg("+r_actorspriteshadow");
	Args->AppendArg("0");
	Args->AppendArg("+r_maxparticles");
	Args->AppendArg("1024");
	// Square particles need no sampled soft mask and can use the solid hardware
	// path when fully opaque, reducing both texture binds and blended overdraw.
	Args->AppendArg("+gl_particles_style");
	Args->AppendArg("0");
	// Keep one useful portal/mirror level, but prevent recursively rebuilding
	// several complete scenes in a single handheld frame.
	Args->AppendArg("+r_portal_recursions");
	Args->AppendArg("1");
	Args->AppendArg("+r_mirror_recursions");
	Args->AppendArg("1");
	// Legend of Doom has one fixed protagonist and one authored transition.
	// Command-line defaults also migrate existing SD configurations that still
	// contain GZDoom's old "Player" name or Melt wipe from an earlier build.
	Args->AppendArg("+name");
	Args->AppendArg("Link");
	Args->AppendArg("+wipetype");
	Args->AppendArg("2");
	#ifdef LOD3DS_SAFE_SOFTWARE_SILENT
	// First establish a repeatable real-hardware baseline with no DSP thread or
	// decoder allocations. Audio can be re-enabled after this path passes.
	Args->AppendArg("-nosound");
	Args->AppendArg("-nomusic");
	#endif
	// Diagnostic-capable profiles enter MAP01 directly. The separate silent
	// profile can isolate audio, while the hardware candidate keeps sound and
	// still exercises the physical world renderer in one deterministic run.
#ifdef LOD3DS_HARDWARE_DIAGNOSTIC
	#ifdef LOD3DS_HARDWARE_DIAGNOSTIC_SILENT
	Args->AppendArg("-nosound");
	#endif
	Args->AppendArg("+vid_fps");
	Args->AppendArg("1");
	Args->AppendArg("+logfile");
	Args->AppendArg("sdmc:/3ds/legend-of-doom/boot.log");
	// Exercise the actual world renderer in the same run. The prior title-only
	// diagnostic could prove startup but could not distinguish a missing 2D
	// title texture from a broken 3D scene.
	Args->AppendArg("+map");
	Args->AppendArg("MAP01");
#endif
	progdir = DataDirectory;
	I_3DSStartupLog("command-line-ready");
#else

	// Should we even be doing anything with progdir on Unix systems?
	char program[PATH_MAX];
	if (realpath (argv[0], program) == NULL)
		strcpy (program, argv[0]);
	char *slash = strrchr (program, '/');
	if (slash != NULL)
	{
		*(slash + 1) = '\0';
		progdir = program;
	}
	else
	{
		progdir = "./";
	}
#endif
	
	I_StartupJoysticks();
#ifdef __3DS__
	I_3DSStartupLog("joystick-ready");
	I_3DSStartupLog("game-main-enter");
#endif

	const int result = GameMain();

	#ifdef __3DS__
	I_3DSStartupLog("game-main-returned");
	#endif

	SDL_Quit();

	#ifdef __3DS__
	// Keep the emergency exit path alive through SDL/gfx teardown; an earlier
	// PICA build could otherwise wedge here after GameMain had already returned.
	I_3DSStopExitSupervisor();
	#endif

	return result;
}
