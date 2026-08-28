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

#ifndef LOD3DS_BUILD_ID
#define LOD3DS_BUILD_ID "untracked"
#endif

#ifndef LOD3DS_BUILD_PROFILE_NAME
#define LOD3DS_BUILD_PROFILE_NAME "release"
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
	Printf("[lod3ds] build_id=%s profile=%s\n",
		LOD3DS_BUILD_ID, LOD3DS_BUILD_PROFILE_NAME);
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
	printf("[lod3ds] build_id=%s profile=%s\n",
		LOD3DS_BUILD_ID, LOD3DS_BUILD_PROFILE_NAME);
#endif

	setlocale (LC_ALL, "C");

	if (SDL_Init (0) < 0)
	{
		fprintf (stderr, "Could not initialize SDL:\n%s\n", SDL_GetError());
		return -1;
	}
#ifdef __3DS__
	// Do not initialize or write an LCD framebuffer before SDLVideo owns it.
	// On real New 3DS hardware SDL's early framebuffer can reside in a VRAM
	// section that is not CPU-writable.  The old diagnostic loading screen
	// wrote to it directly and crashed at LoadingRect() before the game opened
	// (Luma dump 00000101, PC 0x00106a30, FAR 0x1f3002cd).  Startup progress is
	// still recorded to startup.log; SDLVideo initializes video at the normal
	// renderer boundary later in startup.
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
	Args->AppendArg("400");
	Args->AppendArg("-height");
	Args->AppendArg("240");
	Args->AppendArg("+vid_preferbackend");
	Args->AppendArg("3");
	Args->AppendArg("+vid_rendermode");
	Args->AppendArg("4");
	Args->AppendArg("+r_multithreaded");
	Args->AppendArg("0");
	// Keep the original pixel-art presentation and avoid the 33% mip-chain
	// VRAM overhead on the PICA200 texture budget.
	Args->AppendArg("+gl_texture_filter");
	Args->AppendArg("0");
	Args->AppendArg("+gl_texture_filter_anisotropic");
	Args->AppendArg("1");
	Args->AppendArg("+gl_texture_hqresizemode");
	Args->AppendArg("0");
	Args->AppendArg("+gl_precache");
	Args->AppendArg("0");
	// New 3DS baseline: spend CPU/GPU time on the world renderer, not desktop
	// post effects or dynamic-light preparation that NovaGL cannot accelerate.
	// These command-line values also override stale desktop settings on the SD.
	// Pace the New 3DS renderer at 30 FPS.  Leaving vid_maxfps unlimited makes
	// the CPU and PICA200 build frames that cannot improve the 30 FPS target,
	// reducing the headroom available for texture streaming and audio.
	Args->AppendArg("+vid_vsync");
	Args->AppendArg("1");
	Args->AppendArg("+vid_maxfps");
	Args->AppendArg("30");
	Args->AppendArg("+cl_capfps");
	Args->AppendArg("0");
	Args->AppendArg("+gl_multithread");
	Args->AppendArg("0");
	Args->AppendArg("+gl_lights");
	Args->AppendArg("0");
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

	return result;
}
