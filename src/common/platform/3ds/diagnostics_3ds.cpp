#include "diagnostics_3ds.h"
#include "cache_3ds.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

#include "gitinfo.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "common/fonts/v_font.h"
#include "common/menu/menu.h"
#include "common/textures/bitmap.h"
#include "common/textures/gametexture.h"
#include "common/textures/texturemanager.h"
#include "doomdata.h"
#include "doomstat.h"
#include "d_eventbase.h"
#include "d_gui.h"
#include "g_levellocals.h"
#include "gamestate.h"
#include "g_statusbar/sbar.h"
#include "i_sound.h"
#include "menu/doommenu.h"
#include "menustate.h"
#include "playsim/actor.h"
#include "playsim/d_player.h"
#include "r_videoscale.h"
#include "rendering/r_sky.h"
#include "s_music.h"
#include "version.h"
#include "aim_crosshair.inc"

EXTERN_CVAR(Int, vid_maxfps)
EXTERN_CVAR(Bool, vid_fps)
EXTERN_CVAR(Int, lod3ds_render_scale)
EXTERN_CVAR(Bool, lod3ds_top_hud)
EXTERN_CVAR(Bool, crosshairon)

CVAR(Bool, lod3ds_select_overlay, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, lod3ds_map_collisions, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
extern uint64_t LastCount;
extern double LastFrameMilliseconds;

#ifndef LOD3DS_BUILD_PROFILE_NAME
#define LOD3DS_BUILD_PROFILE_NAME "release"
#endif

#ifndef LOD3DS_PORT_VERSION
#define LOD3DS_PORT_VERSION "dev"
#endif

namespace
{
constexpr const char *AppDirectory = "sdmc:/3ds/legend-of-doom";
constexpr const char *DumpDirectory = "sdmc:/3ds/legend-of-doom/dumps";
constexpr const char *BootLogPath = "sdmc:/3ds/legend-of-doom/boot.log";
constexpr const char *StartupLogPath = "sdmc:/3ds/legend-of-doom/startup.log";
constexpr const char *FatalLogPath = "sdmc:/3ds/legend-of-doom/fatal.log";
constexpr const char *ConfigPath = "sdmc:/3ds/legend-of-doom/legend-of-doom.ini";
constexpr const char *TelemetryPath = "sdmc:/3ds/legend-of-doom/frame-telemetry.csv";
constexpr const char *BuildManifestPath = "sdmc:/3ds/legend-of-doom/BUILD-MANIFEST.txt";
constexpr const char *RomfsBuildManifestPath = "romfs:/BUILD-MANIFEST.txt";
constexpr const char *NovaLitePath = "sdmc:/3ds/legend-of-doom/nova-lite.csv";
constexpr const char *NovaGLLogPath = "sdmc:/3ds/legend-of-doom/novagl.log";
constexpr unsigned TelemetryFramesPerSegment = 720;

constexpr uint32_t ShoulderButtonMask = (1u << 8) | (1u << 9); // R + L
constexpr uint32_t QuickDumpButtonMask = ShoulderButtonMask | (1u << 0); // L + R + A
constexpr uint32_t FullDumpButtonMask = ShoulderButtonMask | (1u << 10); // L + R + X
constexpr uint32_t CleanDumpButtonMask = ShoulderButtonMask | (1u << 11); // L + R + Y
constexpr uintptr_t UserAddressLimit = 0x40000000u;
constexpr size_t IoChunkBytes = 512u * 1024u;
constexpr uint64_t DumpProgressStepBytes = 4u * 1024u * 1024u;
constexpr uint64_t FreeSpaceReserveBytes = 8u * 1024u * 1024u;
constexpr uint64_t DumpMetadataBudgetBytes = 4u * 1024u * 1024u;
constexpr size_t MaxCopiedLogBytes = 4u * 1024u * 1024u;
constexpr size_t MaxCopiedTelemetryBytes = 16u * 1024u * 1024u;
constexpr size_t MaxCopiedRendererLogBytes = 64u * 1024u * 1024u;
constexpr size_t MaxConfigInputBytes = 2u * 1024u * 1024u;
constexpr unsigned MaxMemoryRegions = 2048;
constexpr uint64_t DumpUiDelayMilliseconds = 1000;

enum class EDiagnosticDumpMode : unsigned
{
	None,
	Quick,
	Full,
	Clean,
};

std::atomic<unsigned> DumpRequestedMode{static_cast<unsigned>(EDiagnosticDumpMode::None)};
std::atomic<bool> DumpRunning{false};
std::atomic<unsigned> DumpSerial{0};
std::atomic<uint64_t> DumpRequestNotBeforeMilliseconds{0};
std::atomic<unsigned> SkyFallbackCalls{0};
std::atomic<unsigned> SkyFallbackFilledPixels{0};
std::atomic<unsigned> SkyFallbackLastFilledPixels{0};
std::atomic<unsigned> SkyColumnRepairColumns{0};
std::atomic<unsigned> SkyColumnRepairPixels{0};
std::atomic<unsigned> SkyColumnRepairLastColumns{0};
std::atomic<unsigned> SkyColumnRepairLastPixels{0};
std::atomic<unsigned> FlatSkyBackgroundCalls{0};
std::atomic<unsigned> FlatSkyBackgroundPixels{0};
std::atomic<unsigned> FlatSkyPortalPlanesSkipped{0};
std::atomic<uint64_t> SkyViewpointPortalPlanesSkipped{0};
std::atomic<uint64_t> DrawDistanceBspSubtreesCulled{0};
std::atomic<uint64_t> DrawDistanceLinesCulled{0};
std::atomic<uint64_t> DrawDistanceSpritesCulled{0};
std::atomic<uint64_t> DrawDistanceFogPixels{0};
uint32_t HeldButtons;
uint32_t SuppressedComboMask;

constexpr unsigned BottomScreenWidth = 320;
constexpr unsigned BottomScreenHeight = 240;
constexpr uint64_t OverlayRefreshMilliseconds = 125;
constexpr uint64_t OverlayNotificationMilliseconds = 4000;
uint64_t OverlayLastDrawMilliseconds;
uint64_t OverlayNotificationUntilMilliseconds;
bool OverlayAudioReady;
char OverlayNotification[48];
bool DiagnosticProgressActive;
EDiagnosticDumpMode DiagnosticProgressMode = EDiagnosticDumpMode::None;
uint64_t DiagnosticProgressValue;
uint64_t DiagnosticProgressMaximum;
char DiagnosticProgressStage[32];

enum class EBottomPresentation : unsigned
{
	Unknown,
	Blank,
	Menu,
	MenuDimmed,
	NativeMenu,
	DeveloperOverlay,
	Gameplay,
	Progress,
};

EBottomPresentation BottomPresentation = EBottomPresentation::Unknown;
bool MenuStoryPage;

enum class EBottomGameplayTab : unsigned
{
	Map,
	Items,
};

EBottomGameplayTab BottomGameplayTab = EBottomGameplayTab::Map;
bool BottomMapZoomedOut;
bool DeveloperOverlayVisible;
bool NativeMenuTransformValid;
float NativeMenuSourceLeft;
float NativeMenuSourceTop;
float NativeMenuTargetLeft;
float NativeMenuTargetTop;
float NativeMenuScale = 0.8f;
struct FNativeMenuTouchRow
{
	int Top;
	int Bottom;
	int Item;
};
std::array<FNativeMenuTouchRow, 10> NativeMenuTouchRows;
unsigned NativeMenuTouchRowCount;
bool NativeMenuCustomList;
bool NativeMenuCustomOption;
bool NativeMenuCustomSave;

bool LoadingScreenActive;
bool LoadingScreenFinished;
unsigned LoadingScreenProgress;

#if defined(LOD3DS_HARDWARE_DIAGNOSTIC) && LOD3DS_HARDWARE_DIAGNOSTIC
TickCounter FrameTelemetryTimer;
unsigned long long FrameTelemetrySerial;
bool FrameTelemetryRunning;
unsigned FrameTelemetryDraws;
unsigned FrameTelemetryVertices;
unsigned FrameTelemetryTriangles;
unsigned FrameTelemetryFans;
unsigned FrameTelemetryStrips;
unsigned FrameTelemetryIndexed;
constexpr size_t FrameTelemetryBufferBytes = 192u * 1024u;
char FrameTelemetryBuffer[FrameTelemetryBufferBytes];
size_t FrameTelemetryBufferUsed;
#endif

alignas(16) unsigned char IoBuffer[IoChunkBytes];

class FScopedDiagnosticAudioPause
{
public:
	FScopedDiagnosticAudioPause()
		: ResumeMusicOnExit(!S_IsMusicPaused()), Renderer(GSnd)
	{
		if (ResumeMusicOnExit) S_PauseMusic();
		// A full dump blocks the main thread on SD writes for up to several
		// minutes. Pausing only the music stream left sound effects, no-pause
		// channels and queued NDSP buffers audible, and could starve the stream
		// feeder. Freeze the complete OpenAL device and resume it cleanly after
		// the atomic dump has finished.
		if (Renderer != nullptr)
			Renderer->SetInactive(SoundRenderer::INACTIVE_Complete);
	}

	~FScopedDiagnosticAudioPause()
	{
		if (Renderer != nullptr && Renderer == GSnd)
			Renderer->SetInactive(SoundRenderer::INACTIVE_Active);
		if (ResumeMusicOnExit) S_ResumeMusic();
	}

	FScopedDiagnosticAudioPause(const FScopedDiagnosticAudioPause &) = delete;
	FScopedDiagnosticAudioPause &operator=(const FScopedDiagnosticAudioPause &) = delete;

private:
	bool ResumeMusicOnExit;
	SoundRenderer *Renderer;
};

struct FScreenSnapshot
{
	FScreenSnapshot() = default;

	~FScreenSnapshot()
	{
		Release();
	}

	void Release()
	{
		if (Pixels != nullptr) linearFree(Pixels);
		Width = 0;
		Height = 0;
		BytesPerPixel = 0;
		Size = 0;
		Pixels = nullptr;
	}

	FScreenSnapshot(const FScreenSnapshot &) = delete;
	FScreenSnapshot &operator=(const FScreenSnapshot &) = delete;

	uint16_t Width = 0;
	uint16_t Height = 0;
	GSPGPU_FramebufferFormat Format = GSP_RGBA8_OES;
	unsigned BytesPerPixel = 0;
	size_t Size = 0;
	unsigned char *Pixels = nullptr;
};

#include "triforce_frames.inc"
#include "menu_bottom_screen.inc"
#include "bottom_game_interface.inc"

constexpr size_t LoadingAnimationStackBytes = 24 * 1024;
std::atomic<bool> LoadingAnimationStop{false};
Thread LoadingAnimationThread;

bool EnsureDirectory(const char *path);

#if defined(LOD3DS_HARDWARE_DIAGNOSTIC) && LOD3DS_HARDWARE_DIAGNOSTIC
bool FlushFrameTelemetry()
{
	if (FrameTelemetryBufferUsed == 0 || !EnsureDirectory(AppDirectory)) return false;
	FILE *csv = std::fopen(TelemetryPath, "w");
	if (csv == nullptr) return false;
	const bool ok = std::fwrite(FrameTelemetryBuffer, 1, FrameTelemetryBufferUsed, csv) ==
		FrameTelemetryBufferUsed && std::fflush(csv) == 0;
	if (std::fclose(csv) != 0) return false;
	return ok;
}
#endif

struct FLoadingGlyph
{
	char Character;
	uint8_t Rows[7];
};

// Compact 5x7 font containing only the characters used by the loading UI.
// Keeping it here avoids starting the shared-font service or allocating an
// SDL surface while the engine is still parsing its data archives.
constexpr FLoadingGlyph LoadingFont[] = {
	{ '0', { 14, 17, 19, 21, 25, 17, 14 } },
	{ '1', { 4, 12, 4, 4, 4, 4, 14 } },
	{ '2', { 14, 17, 1, 2, 4, 8, 31 } },
	{ '3', { 30, 1, 1, 14, 1, 1, 30 } },
	{ '4', { 2, 6, 10, 18, 31, 2, 2 } },
	{ '5', { 31, 16, 16, 30, 1, 1, 30 } },
	{ '6', { 14, 16, 16, 30, 17, 17, 14 } },
	{ '7', { 31, 1, 2, 4, 8, 8, 8 } },
	{ '8', { 14, 17, 17, 14, 17, 17, 14 } },
	{ '9', { 14, 17, 17, 15, 1, 1, 14 } },
	{ 'A', { 14, 17, 17, 31, 17, 17, 17 } },
	{ 'B', { 30, 17, 17, 30, 17, 17, 30 } },
	{ 'C', { 14, 17, 16, 16, 16, 17, 14 } },
	{ 'D', { 30, 17, 17, 17, 17, 17, 30 } },
	{ 'E', { 31, 16, 16, 30, 16, 16, 31 } },
	{ 'F', { 31, 16, 16, 30, 16, 16, 16 } },
	{ 'G', { 14, 17, 16, 23, 17, 17, 15 } },
	{ 'H', { 17, 17, 17, 31, 17, 17, 17 } },
	{ 'I', { 14, 4, 4, 4, 4, 4, 14 } },
	{ 'J', { 1, 1, 1, 1, 17, 17, 14 } },
	{ 'K', { 17, 18, 20, 24, 20, 18, 17 } },
	{ 'L', { 16, 16, 16, 16, 16, 16, 31 } },
	{ 'M', { 17, 27, 21, 21, 17, 17, 17 } },
	{ 'N', { 17, 25, 21, 19, 17, 17, 17 } },
	{ 'O', { 14, 17, 17, 17, 17, 17, 14 } },
	{ 'P', { 30, 17, 17, 30, 16, 16, 16 } },
	{ 'Q', { 14, 17, 17, 17, 21, 18, 13 } },
	{ 'R', { 30, 17, 17, 30, 20, 18, 17 } },
	{ 'S', { 15, 16, 16, 14, 1, 1, 30 } },
	{ 'T', { 31, 4, 4, 4, 4, 4, 4 } },
	{ 'U', { 17, 17, 17, 17, 17, 17, 14 } },
	{ 'V', { 17, 17, 17, 17, 17, 10, 4 } },
	{ 'W', { 17, 17, 17, 21, 21, 21, 10 } },
	{ 'X', { 17, 17, 10, 4, 10, 17, 17 } },
	{ 'Y', { 17, 17, 10, 4, 4, 4, 4 } },
	{ 'Z', { 31, 1, 2, 4, 8, 16, 31 } },
	{ '%', { 17, 2, 4, 8, 16, 17, 0 } },
	{ '-', { 0, 0, 0, 31, 0, 0, 0 } },
	{ '+', { 0, 4, 4, 31, 4, 4, 0 } },
	{ '>', { 16, 8, 4, 2, 4, 8, 16 } },
	{ '.', { 0, 0, 0, 0, 0, 6, 6 } },
	{ '/', { 1, 2, 4, 8, 16, 0, 0 } },
	{ ':', { 0, 4, 4, 0, 4, 4, 0 } },
	{ '?', { 14, 17, 1, 2, 4, 0, 4 } },
};

void LoadingPutPixel(unsigned char *framebuffer, int x, int y,
	unsigned char red, unsigned char green, unsigned char blue)
{
	if (framebuffer == nullptr || x < 0 || x >= 400 || y < 0 || y >= 240) return;
	const size_t offset = 3u * (static_cast<size_t>(x) * 240u + (239u - static_cast<unsigned>(y)));
	framebuffer[offset + 0] = blue;
	framebuffer[offset + 1] = green;
	framebuffer[offset + 2] = red;
}

void LoadingRect(unsigned char *framebuffer, int x, int y, int width, int height,
	unsigned char red, unsigned char green, unsigned char blue)
{
	for (int px = std::max(0, x); px < std::min(400, x + width); ++px)
	{
		for (int py = std::max(0, y); py < std::min(240, y + height); ++py)
		{
			LoadingPutPixel(framebuffer, px, py, red, green, blue);
		}
	}
}

const uint8_t *LoadingFindGlyph(char character)
{
	for (const auto &glyph : LoadingFont)
	{
		if (glyph.Character == character) return glyph.Rows;
	}
	return nullptr;
}

int LoadingTextWidth(const char *text, int scale)
{
	return text == nullptr ? 0 : static_cast<int>(std::strlen(text)) * 6 * scale - scale;
}

void LoadingText(unsigned char *framebuffer, int x, int y, const char *text, int scale,
	unsigned char red, unsigned char green, unsigned char blue)
{
	if (text == nullptr || scale <= 0) return;
	for (; *text != '\0'; ++text, x += 6 * scale)
	{
		const uint8_t *rows = LoadingFindGlyph(*text);
		if (rows == nullptr) continue;
		for (int row = 0; row < 7; ++row)
		{
			for (int column = 0; column < 5; ++column)
			{
				if ((rows[row] & (1u << (4 - column))) != 0)
				{
					LoadingRect(framebuffer, x + column * scale, y + row * scale,
						scale, scale, red, green, blue);
				}
			}
		}
	}
}

void TriforcePutPixel(unsigned char *framebuffer, int x, int y, uint16_t color)
{
	if (framebuffer == nullptr || x < 0 || x >= 400 || y < 0 || y >= 240) return;
	const unsigned red5 = (color >> 11) & 31u;
	const unsigned green6 = (color >> 5) & 63u;
	const unsigned blue5 = color & 31u;
	const size_t offset = 4u * (static_cast<size_t>(x) * 240u +
		(239u - static_cast<unsigned>(y)));
	framebuffer[offset + 0] = 255;
	framebuffer[offset + 1] = static_cast<unsigned char>(blue5 * 255u / 31u);
	framebuffer[offset + 2] = static_cast<unsigned char>(green6 * 255u / 63u);
	framebuffer[offset + 3] = static_cast<unsigned char>(red5 * 255u / 31u);
}

bool DrawTriforceAnimationFrame(unsigned frame)
{
	u16 physicalWidth = 0;
	u16 physicalHeight = 0;
	unsigned char *framebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT,
		&physicalWidth, &physicalHeight);
	if (framebuffer == nullptr || physicalWidth != 240 || physicalHeight != 400)
	{
		return false;
	}
	std::memset(framebuffer, 0, 400u * 240u * 4u);
	frame %= TriforceAnimationFrames;
	size_t cursor = TriforceAnimationOffsets[frame];
	const size_t end = TriforceAnimationOffsets[frame + 1];
	constexpr int originX = (400 - static_cast<int>(TriforceAnimationWidth)) / 2;
	constexpr int originY = (240 - static_cast<int>(TriforceAnimationHeight)) / 2;
	while (cursor < end)
	{
		const unsigned y = TriforceAnimationData[cursor++];
		const unsigned x = TriforceAnimationData[cursor++];
		const unsigned length = TriforceAnimationData[cursor++];
		for (unsigned column = 0; column < length; ++column)
		{
			const uint16_t color = static_cast<uint16_t>(TriforceAnimationData[cursor]) |
				(static_cast<uint16_t>(TriforceAnimationData[cursor + 1]) << 8);
			cursor += 2;
			TriforcePutPixel(framebuffer, originX + static_cast<int>(x + column),
				originY + static_cast<int>(y), color);
		}
	}
	I_3DSCleanDataCache(framebuffer, 400u * 240u * 4u);
	gfxScreenSwapBuffers(GFX_TOP, false);
	return true;
}

void TriforceAnimationMain(void *)
{
	unsigned frame = 1;
	while (!LoadingAnimationStop.load(std::memory_order_acquire))
	{
		DrawTriforceAnimationFrame(frame);
		gspWaitForVBlank();
		const uint64_t duration = TriforceAnimationDurations[frame] > 17 ?
			TriforceAnimationDurations[frame] - 17 : 1;
		svcSleepThread(duration * 1000ULL * 1000ULL);
		frame = (frame + 1) % TriforceAnimationFrames;
	}
}

struct FOverlayColor
{
	unsigned char Red;
	unsigned char Green;
	unsigned char Blue;
};

// SDL/libctru's RGBA8888 framebuffer is A, B, G, R in little-endian memory.
// The frame keeps the electric-blue identity while the automap uses distinct
// semantic colors so walls, specials and the player remain readable at a glance.
constexpr FOverlayColor OverlayParchment{ 0, 0, 0 };
constexpr FOverlayColor OverlayParchmentDark{ 0, 0, 0 };
constexpr FOverlayColor OverlayInk{ 0, 0, 0 };
constexpr FOverlayColor OverlayInkSoft{ 0, 0, 0 };
constexpr FOverlayColor OverlayIvory{ 255, 255, 255 };
constexpr FOverlayColor OverlayGold{ 240, 181, 72 };
constexpr FOverlayColor OverlayRed{ 238, 63, 55 };
constexpr FOverlayColor OverlayGreen{ 58, 214, 114 };
constexpr FOverlayColor OverlayBlue{ 22, 62, 255 };

void OverlayFrame(unsigned char *framebuffer, int x, int y, int width, int height,
	int thickness, FOverlayColor color);
void OverlayText(unsigned char *framebuffer, int x, int y, const char *text,
	int scale, FOverlayColor color);
void OverlayTextSized(unsigned char *framebuffer, int x, int y, const char *text,
	int glyphWidth, int glyphHeight, int advance, FOverlayColor color);
void OverlayCenteredText(unsigned char *framebuffer, int x, int width, int y,
	const char *text, int scale, FOverlayColor color);

void OverlayPutPixel(unsigned char *framebuffer, int x, int y, FOverlayColor color)
{
	if (framebuffer == nullptr || x < 0 || x >= static_cast<int>(BottomScreenWidth) ||
		y < 0 || y >= static_cast<int>(BottomScreenHeight))
	{
		return;
	}
	const size_t offset = 4u * (static_cast<size_t>(x) * BottomScreenHeight +
		(BottomScreenHeight - 1u - static_cast<unsigned>(y)));
	framebuffer[offset + 0] = 255;
	framebuffer[offset + 1] = color.Blue;
	framebuffer[offset + 2] = color.Green;
	framebuffer[offset + 3] = color.Red;
}

void OverlayRect(unsigned char *framebuffer, int x, int y, int width, int height,
	FOverlayColor color)
{
	for (int px = std::max(0, x); px < std::min(static_cast<int>(BottomScreenWidth), x + width); ++px)
	{
		for (int py = std::max(0, y); py < std::min(static_cast<int>(BottomScreenHeight), y + height); ++py)
		{
			OverlayPutPixel(framebuffer, px, py, color);
		}
	}
}

FOverlayColor DimMenuColor(FOverlayColor color, unsigned brightness)
{
	return FOverlayColor{
		static_cast<unsigned char>(color.Red * brightness / 255u),
		static_cast<unsigned char>(color.Green * brightness / 255u),
		static_cast<unsigned char>(color.Blue * brightness / 255u),
	};
}

void DrawMenuBottomScreen(unsigned char *framebuffer, unsigned brightness = 255u)
{
	size_t cursor = 0;
	unsigned pixel = 0;
	while (cursor < sizeof(MenuBottomData) && pixel < MenuBottomPixelCount)
	{
		const uint8_t header = MenuBottomData[cursor++];
		const unsigned count = (header & 0x7fu) + 1u;
		if ((header & 0x80u) != 0)
		{
			if (cursor + 2u > sizeof(MenuBottomData)) break;
			const uint16_t color = static_cast<uint16_t>(MenuBottomData[cursor]) |
				(static_cast<uint16_t>(MenuBottomData[cursor + 1u]) << 8u);
			cursor += 2u;
			const FOverlayColor expanded = DimMenuColor(FOverlayColor{
				static_cast<unsigned char>(((color >> 11u) & 0x1fu) * 255u / 31u),
				static_cast<unsigned char>(((color >> 5u) & 0x3fu) * 255u / 63u),
				static_cast<unsigned char>((color & 0x1fu) * 255u / 31u),
			}, brightness);
			for (unsigned run = 0; run < count && pixel < MenuBottomPixelCount;
				++run, ++pixel)
			{
				OverlayPutPixel(framebuffer, pixel % MenuBottomWidth,
					pixel / MenuBottomWidth, expanded);
			}
		}
		else
		{
			if (cursor + count * 2u > sizeof(MenuBottomData)) break;
			for (unsigned literal = 0; literal < count && pixel < MenuBottomPixelCount;
				++literal, ++pixel)
			{
				const uint16_t color = static_cast<uint16_t>(MenuBottomData[cursor]) |
					(static_cast<uint16_t>(MenuBottomData[cursor + 1u]) << 8u);
				cursor += 2u;
				OverlayPutPixel(framebuffer, pixel % MenuBottomWidth,
					pixel / MenuBottomWidth,
					DimMenuColor(FOverlayColor{
						static_cast<unsigned char>(((color >> 11u) & 0x1fu) * 255u / 31u),
						static_cast<unsigned char>(((color >> 5u) & 0x3fu) * 255u / 63u),
						static_cast<unsigned char>((color & 0x1fu) * 255u / 31u),
					}, brightness));
			}
		}
	}
	if (pixel < MenuBottomPixelCount)
	{
		OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight,
			OverlayParchment);
	}
}

struct FEmbeddedBottomImage
{
	unsigned Width;
	unsigned Height;
	unsigned PixelCount;
	const uint8_t *Data;
	size_t DataSize;
};

#define BOTTOM_IMAGE(name) \
	FEmbeddedBottomImage{ name##Width, name##Height, name##PixelCount, name##Data, sizeof(name##Data) }

void DrawEmbeddedBottomImage(unsigned char *framebuffer, int x, int y,
	const FEmbeddedBottomImage &image)
{
	size_t cursor = 0;
	unsigned pixel = 0;
	while (cursor < image.DataSize && pixel < image.PixelCount)
	{
		const uint8_t header = image.Data[cursor++];
		const unsigned count = (header & 0x7fu) + 1u;
		if ((header & 0x80u) != 0)
		{
			if (cursor + 2u > image.DataSize) break;
			const uint16_t color = static_cast<uint16_t>(image.Data[cursor]) |
				(static_cast<uint16_t>(image.Data[cursor + 1u]) << 8u);
			cursor += 2u;
			for (unsigned run = 0; run < count && pixel < image.PixelCount; ++run, ++pixel)
			{
				if ((color & 0x8000u) != 0)
				{
					OverlayPutPixel(framebuffer, x + static_cast<int>(pixel % image.Width),
						y + static_cast<int>(pixel / image.Width), FOverlayColor{
							static_cast<unsigned char>(((color >> 10u) & 0x1fu) * 255u / 31u),
							static_cast<unsigned char>(((color >> 5u) & 0x1fu) * 255u / 31u),
							static_cast<unsigned char>((color & 0x1fu) * 255u / 31u),
						});
				}
			}
		}
		else
		{
			if (cursor + count * 2u > image.DataSize) break;
			for (unsigned literal = 0; literal < count && pixel < image.PixelCount;
				++literal, ++pixel)
			{
				const uint16_t color = static_cast<uint16_t>(image.Data[cursor]) |
					(static_cast<uint16_t>(image.Data[cursor + 1u]) << 8u);
				cursor += 2u;
				if ((color & 0x8000u) != 0)
				{
					OverlayPutPixel(framebuffer, x + static_cast<int>(pixel % image.Width),
						y + static_cast<int>(pixel / image.Width), FOverlayColor{
							static_cast<unsigned char>(((color >> 10u) & 0x1fu) * 255u / 31u),
							static_cast<unsigned char>(((color >> 5u) & 0x1fu) * 255u / 31u),
							static_cast<unsigned char>((color & 0x1fu) * 255u / 31u),
						});
				}
			}
		}
	}
}

void DrawEmbeddedBottomImageScaled(unsigned char *framebuffer, int x, int y,
	const FEmbeddedBottomImage &image, unsigned scale)
{
	if (scale <= 1u)
	{
		DrawEmbeddedBottomImage(framebuffer, x, y, image);
		return;
	}

	size_t cursor = 0;
	unsigned pixel = 0;
	auto drawPixel = [&](uint16_t color)
	{
		if ((color & 0x8000u) != 0)
		{
			const FOverlayColor expanded{
				static_cast<unsigned char>(((color >> 10u) & 0x1fu) * 255u / 31u),
				static_cast<unsigned char>(((color >> 5u) & 0x1fu) * 255u / 31u),
				static_cast<unsigned char>((color & 0x1fu) * 255u / 31u),
			};
			OverlayRect(framebuffer,
				x + static_cast<int>((pixel % image.Width) * scale),
				y + static_cast<int>((pixel / image.Width) * scale),
				static_cast<int>(scale), static_cast<int>(scale), expanded);
		}
		++pixel;
	};
	while (cursor < image.DataSize && pixel < image.PixelCount)
	{
		const uint8_t header = image.Data[cursor++];
		const unsigned count = (header & 0x7fu) + 1u;
		if ((header & 0x80u) != 0)
		{
			if (cursor + 2u > image.DataSize) break;
			const uint16_t color = static_cast<uint16_t>(image.Data[cursor]) |
				(static_cast<uint16_t>(image.Data[cursor + 1u]) << 8u);
			cursor += 2u;
			for (unsigned run = 0; run < count && pixel < image.PixelCount; ++run)
				drawPixel(color);
		}
		else
		{
			if (cursor + count * 2u > image.DataSize) break;
			for (unsigned literal = 0; literal < count && pixel < image.PixelCount; ++literal)
			{
				const uint16_t color = static_cast<uint16_t>(image.Data[cursor]) |
					(static_cast<uint16_t>(image.Data[cursor + 1u]) << 8u);
				cursor += 2u;
				drawPixel(color);
			}
		}
	}
}

void DrawEmbeddedBottomImageSized(unsigned char *framebuffer, int x, int y,
	const FEmbeddedBottomImage &image, int targetWidth, int targetHeight)
{
	if (targetWidth <= 0 || targetHeight <= 0 || image.Width == 0 || image.Height == 0)
		return;

	size_t cursor = 0;
	unsigned pixel = 0;
	auto drawPixel = [&](uint16_t color)
	{
		if ((color & 0x8000u) != 0)
		{
			const int sourceX = static_cast<int>(pixel % image.Width);
			const int sourceY = static_cast<int>(pixel / image.Width);
			const int left = x + sourceX * targetWidth / static_cast<int>(image.Width);
			const int top = y + sourceY * targetHeight / static_cast<int>(image.Height);
			const int right = x + (sourceX + 1) * targetWidth /
				static_cast<int>(image.Width);
			const int bottom = y + (sourceY + 1) * targetHeight /
				static_cast<int>(image.Height);
			OverlayRect(framebuffer, left, top, std::max(1, right - left),
				std::max(1, bottom - top), FOverlayColor{
					static_cast<unsigned char>(((color >> 10u) & 0x1fu) * 255u / 31u),
					static_cast<unsigned char>(((color >> 5u) & 0x1fu) * 255u / 31u),
					static_cast<unsigned char>((color & 0x1fu) * 255u / 31u),
				});
		}
		++pixel;
	};
	while (cursor < image.DataSize && pixel < image.PixelCount)
	{
		const uint8_t header = image.Data[cursor++];
		const unsigned count = (header & 0x7fu) + 1u;
		if ((header & 0x80u) != 0)
		{
			if (cursor + 2u > image.DataSize) break;
			const uint16_t color = static_cast<uint16_t>(image.Data[cursor]) |
				(static_cast<uint16_t>(image.Data[cursor + 1u]) << 8u);
			cursor += 2u;
			for (unsigned run = 0; run < count && pixel < image.PixelCount; ++run)
				drawPixel(color);
		}
		else
		{
			if (cursor + count * 2u > image.DataSize) break;
			for (unsigned literal = 0; literal < count && pixel < image.PixelCount;
				++literal)
			{
				const uint16_t color = static_cast<uint16_t>(image.Data[cursor]) |
					(static_cast<uint16_t>(image.Data[cursor + 1u]) << 8u);
				cursor += 2u;
				drawPixel(color);
			}
		}
	}
}

void DrawBottomGameTexture(unsigned char *framebuffer, int x, int y,
	FGameTexture *texture, int maximumWidth = 0, int maximumHeight = 0)
{
	if (texture == nullptr || texture->GetTexture() == nullptr) return;
	FBitmap bitmap = texture->GetTexture()->GetBgraBitmap(nullptr);
	const int sourceWidth = bitmap.GetWidth();
	const int sourceHeight = bitmap.GetHeight();
	if (sourceWidth <= 0 || sourceHeight <= 0 || bitmap.GetPixels() == nullptr) return;

	double scale = 1.0;
	if (maximumWidth > 0 && sourceWidth > maximumWidth)
		scale = std::min(scale, static_cast<double>(maximumWidth) / sourceWidth);
	if (maximumHeight > 0 && sourceHeight > maximumHeight)
		scale = std::min(scale, static_cast<double>(maximumHeight) / sourceHeight);
	const int targetWidth = std::max(1, static_cast<int>(sourceWidth * scale + 0.5));
	const int targetHeight = std::max(1, static_cast<int>(sourceHeight * scale + 0.5));
	const uint8_t *pixels = bitmap.GetPixels();
	for (int targetY = 0; targetY < targetHeight; ++targetY)
	{
		const int sourceY = std::min(sourceHeight - 1, targetY * sourceHeight / targetHeight);
		for (int targetX = 0; targetX < targetWidth; ++targetX)
		{
			const int sourceX = std::min(sourceWidth - 1, targetX * sourceWidth / targetWidth);
			const uint8_t *pixel = pixels + sourceY * bitmap.GetPitch() + sourceX * 4;
			if (pixel[3] >= 32u)
			{
				OverlayPutPixel(framebuffer, x + targetX, y + targetY,
					FOverlayColor{ pixel[2], pixel[1], pixel[0] });
			}
		}
	}
}

void DrawBottomGameTextureSized(unsigned char *framebuffer, int x, int y,
	FGameTexture *texture, int targetWidth, int targetHeight)
{
	if (texture == nullptr || texture->GetTexture() == nullptr ||
		targetWidth <= 0 || targetHeight <= 0)
	{
		return;
	}
	FBitmap bitmap = texture->GetTexture()->GetBgraBitmap(nullptr);
	const int sourceWidth = bitmap.GetWidth();
	const int sourceHeight = bitmap.GetHeight();
	if (sourceWidth <= 0 || sourceHeight <= 0 || bitmap.GetPixels() == nullptr) return;
	for (int targetY = 0; targetY < targetHeight; ++targetY)
	{
		const int sourceY = std::min(sourceHeight - 1,
			targetY * sourceHeight / targetHeight);
		for (int targetX = 0; targetX < targetWidth; ++targetX)
		{
			const int sourceX = std::min(sourceWidth - 1,
				targetX * sourceWidth / targetWidth);
			const uint8_t *pixel = bitmap.GetPixels() +
				sourceY * bitmap.GetPitch() + sourceX * 4;
			if (pixel[3] >= 32u)
			{
				OverlayPutPixel(framebuffer, x + targetX, y + targetY,
					FOverlayColor{ pixel[2], pixel[1], pixel[0] });
			}
		}
	}
}

int NativeFontTextWidth(FFont *font, const char *text, int scale = 1)
{
	if (font == nullptr || text == nullptr || scale <= 0) return 0;
	return font->StringWidth(text) * scale;
}

void DrawBottomFontText(unsigned char *framebuffer, FFont *font, int x, int y,
	const char *text, int scale, FOverlayColor color)
{
	if (framebuffer == nullptr || font == nullptr || text == nullptr || scale <= 0)
		return;
	const int kerning = font->GetDefaultKerning();
	for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
		*cursor != '\0'; ++cursor)
	{
		int advance = font->GetSpaceWidth();
		FGameTexture *glyph = font->GetChar(*cursor, CR_UNTRANSLATED, &advance);
		if (glyph != nullptr && glyph->GetTexture() != nullptr)
		{
			FBitmap bitmap = glyph->GetTexture()->GetBgraBitmap(nullptr);
			if (bitmap.GetPixels() != nullptr)
			{
				for (int sourceY = 0; sourceY < bitmap.GetHeight(); ++sourceY)
				{
					for (int sourceX = 0; sourceX < bitmap.GetWidth(); ++sourceX)
					{
						const uint8_t alpha = bitmap.GetPixels()[
							sourceY * bitmap.GetPitch() + sourceX * 4 + 3];
						if (alpha < 32u) continue;
						OverlayRect(framebuffer, x + sourceX * scale,
							y + sourceY * scale, scale, scale, color);
					}
				}
			}
		}
		x += std::max(1, advance + kerning) * scale;
	}
}

void DrawBottomCenteredFontText(unsigned char *framebuffer, FFont *font,
	int x, int width, int y, const char *text, int scale, FOverlayColor color)
{
	DrawBottomFontText(framebuffer, font,
		x + (width - NativeFontTextWidth(font, text, scale)) / 2,
		y, text, scale, color);
}

int BottomInventoryAmount(AActor *owner, const char *type)
{
	if (owner == nullptr || type == nullptr || type[0] == '\0') return 0;
	AActor *item = owner->FindInventory(FName(type), false);
	return item != nullptr ? item->IntVar(NAME_Amount) : 0;
}

bool BottomHasAnyInventory(AActor *owner, const char *first,
	const char *second = nullptr, const char *third = nullptr)
{
	return BottomInventoryAmount(owner, first) > 0 ||
		BottomInventoryAmount(owner, second) > 0 ||
		BottomInventoryAmount(owner, third) > 0;
}

void DrawBottomMapLine(unsigned char *framebuffer, int x0, int y0, int x1, int y1,
	FOverlayColor color)
{
	const int deltaX = std::abs(x1 - x0);
	const int stepX = x0 < x1 ? 1 : -1;
	const int deltaY = -std::abs(y1 - y0);
	const int stepY = y0 < y1 ? 1 : -1;
	int error = deltaX + deltaY;
	for (;;)
	{
		OverlayPutPixel(framebuffer, x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		const int twiceError = error * 2;
		if (twiceError >= deltaY)
		{
			error += deltaY;
			x0 += stepX;
		}
		if (twiceError <= deltaX)
		{
			error += deltaX;
			y0 += stepY;
		}
	}
}

bool ClipBottomMapLine(int &x0, int &y0, int &x1, int &y1,
	int left, int top, int right, int bottom)
{
	auto code = [&](int x, int y)
	{
		return (x < left ? 1u : 0u) | (x > right ? 2u : 0u) |
			(y < top ? 4u : 0u) | (y > bottom ? 8u : 0u);
	};
	unsigned first = code(x0, y0);
	unsigned second = code(x1, y1);
	while ((first | second) != 0u)
	{
		if ((first & second) != 0u) return false;
		const unsigned outside = first != 0u ? first : second;
		double x = 0.0;
		double y = 0.0;
		if ((outside & 8u) != 0u)
		{
			y = bottom;
			x = x0 + static_cast<double>(x1 - x0) * (bottom - y0) / (y1 - y0);
		}
		else if ((outside & 4u) != 0u)
		{
			y = top;
			x = x0 + static_cast<double>(x1 - x0) * (top - y0) / (y1 - y0);
		}
		else if ((outside & 2u) != 0u)
		{
			x = right;
			y = y0 + static_cast<double>(y1 - y0) * (right - x0) / (x1 - x0);
		}
		else
		{
			x = left;
			y = y0 + static_cast<double>(y1 - y0) * (left - x0) / (x1 - x0);
		}
		if (outside == first)
		{
			x0 = static_cast<int>(x + 0.5);
			y0 = static_cast<int>(y + 0.5);
			first = code(x0, y0);
		}
		else
		{
			x1 = static_cast<int>(x + 0.5);
			y1 = static_cast<int>(y + 0.5);
			second = code(x1, y1);
		}
	}
	return true;
}

void DrawBottomMapLineClipped(unsigned char *framebuffer, int x0, int y0, int x1, int y1,
	int left, int top, int right, int bottom, FOverlayColor color)
{
	if (ClipBottomMapLine(x0, y0, x1, y1, left, top, right, bottom))
		DrawBottomMapLine(framebuffer, x0, y0, x1, y1, color);
}

void DrawBottomCircle(unsigned char *framebuffer, int centerX, int centerY, int radius,
	FOverlayColor color)
{
	int x = radius;
	int y = 0;
	int error = 1 - radius;
	while (x >= y)
	{
		OverlayPutPixel(framebuffer, centerX + x, centerY + y, color);
		OverlayPutPixel(framebuffer, centerX + y, centerY + x, color);
		OverlayPutPixel(framebuffer, centerX - y, centerY + x, color);
		OverlayPutPixel(framebuffer, centerX - x, centerY + y, color);
		OverlayPutPixel(framebuffer, centerX - x, centerY - y, color);
		OverlayPutPixel(framebuffer, centerX - y, centerY - x, color);
		OverlayPutPixel(framebuffer, centerX + y, centerY - x, color);
		OverlayPutPixel(framebuffer, centerX + x, centerY - y, color);
		++y;
		if (error < 0) error += 2 * y + 1;
		else
		{
			--x;
			error += 2 * (y - x) + 1;
		}
	}
}

void DrawBottomRoundedRectOutline(unsigned char *framebuffer, int x, int y,
	int width, int height, int radius, int thickness, FOverlayColor color)
{
	auto inside = [](int px, int py, int boxWidth, int boxHeight, int boxRadius)
	{
		if (boxWidth <= 0 || boxHeight <= 0) return false;
		const int nearestX = std::clamp(px, boxRadius, boxWidth - 1 - boxRadius);
		const int nearestY = std::clamp(py, boxRadius, boxHeight - 1 - boxRadius);
		const int deltaX = px - nearestX;
		const int deltaY = py - nearestY;
		return deltaX * deltaX + deltaY * deltaY <= boxRadius * boxRadius;
	};
	for (int py = 0; py < height; ++py)
	{
		for (int px = 0; px < width; ++px)
		{
			if (!inside(px, py, width, height, radius)) continue;
			const bool inner = px >= thickness && py >= thickness &&
				px < width - thickness && py < height - thickness &&
				inside(px - thickness, py - thickness, width - thickness * 2,
					height - thickness * 2, std::max(0, radius - thickness));
			if (!inner) OverlayPutPixel(framebuffer, x + px, y + py, color);
		}
	}
}

FOverlayColor BottomMapLineColor(const line_t &line)
{
	if (line.special != 0) return FOverlayColor{ 58, 214, 114 };
	if ((line.flags & ML_SECRET) != 0) return FOverlayColor{ 222, 80, 62 };
	if (line.backsector == nullptr) return FOverlayColor{ 240, 181, 72 };
	if (line.frontsector != nullptr && line.backsector != nullptr &&
		line.backsector->floorplane != line.frontsector->floorplane)
	{
		return FOverlayColor{ 56, 145, 255 };
	}
	if (line.frontsector != nullptr && line.backsector != nullptr &&
		line.backsector->ceilingplane != line.frontsector->ceilingplane)
	{
		return FOverlayColor{ 82, 220, 231 };
	}
	return FOverlayColor{ 190, 196, 211 };
}

struct FBottomMapTexture
{
	int Texture = -1;
	int Width = 0;
	int Height = 0;
	int Pitch = 0;
	std::vector<uint8_t> Pixels;
};

FBottomMapTexture *BottomMapTexture(FTextureID texture)
{
	static std::array<FBottomMapTexture, 24> cache;
	static unsigned replacement;
	static FBottomMapTexture *last = nullptr;
	if (!texture.isValid()) return nullptr;
	if (last != nullptr && last->Texture == texture.GetIndex()) return last;
	for (auto &entry : cache)
	{
		if (entry.Texture == texture.GetIndex()) return last = &entry;
	}
	FGameTexture *gameTexture = TexMan.GetGameTexture(texture, true);
	if (gameTexture == nullptr || gameTexture->GetTexture() == nullptr) return nullptr;
	FBitmap bitmap = gameTexture->GetTexture()->GetBgraBitmap(nullptr);
	if (bitmap.GetPixels() == nullptr || bitmap.GetWidth() <= 0 || bitmap.GetHeight() <= 0)
		return nullptr;
	FBottomMapTexture &entry = cache[replacement++ % cache.size()];
	entry.Texture = texture.GetIndex();
	entry.Width = bitmap.GetWidth();
	entry.Height = bitmap.GetHeight();
	entry.Pitch = entry.Width * 4;
	entry.Pixels.resize(static_cast<size_t>(entry.Pitch) * entry.Height);
	for (int row = 0; row < entry.Height; ++row)
	{
		std::memcpy(entry.Pixels.data() + static_cast<size_t>(row) * entry.Pitch,
			bitmap.GetPixels() + static_cast<size_t>(row) * bitmap.GetPitch(), entry.Pitch);
	}
	return last = &entry;
}

bool BottomPointInsideSubsector(const subsector_t *subsector, double x, double y)
{
	if (subsector == nullptr || subsector->firstline == nullptr || subsector->numlines < 3)
		return false;
	bool inside = false;
	for (uint32_t current = 0, previous = subsector->numlines - 1;
		current < subsector->numlines; previous = current++)
	{
		const vertex_t *a = subsector->firstline[current].v1;
		const vertex_t *b = subsector->firstline[previous].v1;
		if (a == nullptr || b == nullptr) continue;
		const bool crosses = ((a->fY() > y) != (b->fY() > y)) &&
			(x < (b->fX() - a->fX()) * (y - a->fY()) /
				(b->fY() - a->fY()) + a->fX());
		if (crosses) inside = !inside;
	}
	return inside;
}

FOverlayColor BottomFloorTextureColor(sector_t *sector, double worldX, double worldY)
{
	if (sector == nullptr) return FOverlayColor{ 8, 12, 18 };
	FBottomMapTexture *texture = BottomMapTexture(sector->GetTexture(sector_t::floor));
	if (texture == nullptr) return FOverlayColor{ 20, 28, 34 };
	const double scaleX = std::max(0.001, std::abs(sector->GetXScale(sector_t::floor)));
	const double scaleY = std::max(0.001, std::abs(sector->GetYScale(sector_t::floor)));
	int textureX = static_cast<int>(std::floor(
		(worldX + sector->GetXOffset(sector_t::floor)) * scaleX));
	int textureY = static_cast<int>(std::floor(
		(-worldY + sector->GetYOffset(sector_t::floor)) * scaleY));
	textureX %= texture->Width;
	textureY %= texture->Height;
	if (textureX < 0) textureX += texture->Width;
	if (textureY < 0) textureY += texture->Height;
	const uint8_t *pixel = texture->Pixels.data() +
		static_cast<size_t>(textureY) * texture->Pitch + textureX * 4;
	if (pixel[3] < 32u) return FOverlayColor{ 12, 18, 24 };
	const double light = 0.50 + 0.50 * std::clamp(sector->GetFloorLight(), 0, 255) / 255.0;
	return FOverlayColor{
		static_cast<unsigned char>(pixel[2] * light),
		static_cast<unsigned char>(pixel[1] * light),
		static_cast<unsigned char>(pixel[0] * light),
	};
}

void DrawBottomAutomap(unsigned char *framebuffer, int mapX = 31, int mapY = 38,
	int mapWidth = 204, int mapHeight = 156, double requestedWorldUnitsPerPixel = 0.0)
{
	const int MapX = mapX;
	const int MapY = mapY;
	const int MapWidth = mapWidth;
	const int MapHeight = mapHeight;
	const double worldUnitsPerPixel = requestedWorldUnitsPerPixel > 0.0 ?
		requestedWorldUnitsPerPixel : (BottomMapZoomedOut ? 32.0 : 12.0);
	OverlayRect(framebuffer, MapX, MapY, MapWidth, MapHeight, OverlayInk);

	if (primaryLevel == nullptr || primaryLevel->lines.Size() == 0)
	{
		OverlayCenteredText(framebuffer, MapX, MapWidth, MapY + MapHeight / 2 - 4,
			"MAP UNAVAILABLE", 1, OverlayIvory);
		return;
	}

	static FLevelLocals *trackedLevel;
	static FString trackedMapName;
	static bool hasExteriorCenter;
	static bool playerDetached;
	static double centerX;
	static double centerY;
	if (trackedLevel != primaryLevel || trackedMapName.Compare(primaryLevel->MapName) != 0)
	{
		trackedLevel = primaryLevel;
		trackedMapName = primaryLevel->MapName;
		hasExteriorCenter = false;
		playerDetached = false;
	}
	AActor *camera = players[consoleplayer].camera;
	if (camera != nullptr)
	{
		if (!hasExteriorCenter)
		{
			centerX = camera->X();
			centerY = camera->Y();
			hasExteriorCenter = true;
		}
		else
		{
			const double deltaX = camera->X() - centerX;
			const double deltaY = camera->Y() - centerY;
			const double distanceSquared = deltaX * deltaX + deltaY * deltaY;
			const bool outdoors = camera->Sector != nullptr &&
				camera->Sector->GetTexture(sector_t::ceiling) == skyflatnum;
			if (outdoors || distanceSquared <= 3072.0 * 3072.0)
			{
				centerX = camera->X();
				centerY = camera->Y();
				playerDetached = false;
			}
			else
			{
				playerDetached = true;
			}
		}
	}
	if (!hasExteriorCenter)
	{
		OverlayCenteredText(framebuffer, MapX, MapWidth, MapY + MapHeight / 2 - 4,
			"MAP UNAVAILABLE", 1, OverlayIvory);
		return;
	}

	auto transformX = [&](double worldX)
	{
		return static_cast<int>(MapX + MapWidth * 0.5 +
			(worldX - centerX) / worldUnitsPerPixel + 0.5);
	};
	auto transformY = [&](double worldY)
	{
		return static_cast<int>(MapY + MapHeight * 0.5 -
			(worldY - centerY) / worldUnitsPerPixel + 0.5);
	};

	// Sample terrain in 2x2 blocks to bound BSP queries.
	for (int mapY = 0; mapY < MapHeight; mapY += 2)
	{
		const double worldY = centerY -
			(mapY + 1 - MapHeight * 0.5) * worldUnitsPerPixel;
		for (int mapX = 0; mapX < MapWidth; mapX += 2)
		{
			const double worldX = centerX +
				(mapX + 1 - MapWidth * 0.5) * worldUnitsPerPixel;
			subsector_t *subsector = primaryLevel->PointInRenderSubsector(
				DVector2(worldX, worldY));
			if (subsector == nullptr || (subsector->flags & SSECMF_DRAWN) == 0 ||
				(subsector->flags & (SSECF_HOLE | SSECF_POLYORG)) != 0 ||
				subsector->render_sector == nullptr ||
				(subsector->render_sector->MoreFlags & SECMF_HIDDEN) != 0 ||
				!BottomPointInsideSubsector(subsector, worldX, worldY))
			{
				continue;
			}
			OverlayRect(framebuffer, MapX + mapX, MapY + mapY,
				std::min(2, MapWidth - mapX), std::min(2, MapHeight - mapY),
				BottomFloorTextureColor(subsector->render_sector, worldX, worldY));
		}
	}

	if (lod3ds_map_collisions)
	{
		for (const line_t &line : primaryLevel->lines)
		{
			if (line.v1 == nullptr || line.v2 == nullptr ||
				(line.flags & ML_DONTDRAW) != 0 ||
				(line.flags & (ML_MAPPED | ML_REVEALED)) == 0)
			{
				continue;
			}
			DrawBottomMapLineClipped(framebuffer,
				transformX(line.v1->fX()), transformY(line.v1->fY()),
				transformX(line.v2->fX()), transformY(line.v2->fY()),
				MapX, MapY, MapX + MapWidth - 1, MapY + MapHeight - 1,
				BottomMapLineColor(line));
		}
	}

	if (camera != nullptr && !playerDetached)
	{
		const int playerX = transformX(camera->X());
		const int playerY = transformY(camera->Y());
		const double directionX = camera->Angles.Yaw.Cos();
		const double directionY = -camera->Angles.Yaw.Sin();
		const int tipX = playerX + static_cast<int>(directionX * 7.0);
		const int tipY = playerY + static_cast<int>(directionY * 7.0);
		const int leftX = playerX + static_cast<int>((-directionX - directionY) * 3.5);
		const int leftY = playerY + static_cast<int>((directionX - directionY) * 3.5);
		const int rightX = playerX + static_cast<int>((-directionX + directionY) * 3.5);
		const int rightY = playerY + static_cast<int>((-directionX - directionY) * 3.5);
		DrawBottomMapLineClipped(framebuffer, tipX, tipY, leftX, leftY,
			MapX, MapY, MapX + MapWidth - 1, MapY + MapHeight - 1, OverlayRed);
		DrawBottomMapLineClipped(framebuffer, tipX, tipY, rightX, rightY,
			MapX, MapY, MapX + MapWidth - 1, MapY + MapHeight - 1, OverlayRed);
		DrawBottomMapLineClipped(framebuffer, leftX, leftY, rightX, rightY,
			MapX, MapY, MapX + MapWidth - 1, MapY + MapHeight - 1, OverlayIvory);
	}
}

struct FBottomItem
{
	const FEmbeddedBottomImage *Image;
	const char *Inventory1;
	const char *Inventory2;
	const char *Inventory3;
	const char *Ready1;
	const char *Ready2;
	const char *Ready3;
};

bool BottomReadyWeaponMatches(AActor *readyWeapon, const FBottomItem &item)
{
	if (readyWeapon == nullptr) return false;
	const FName readyName = readyWeapon->GetClass()->TypeName;
	return (item.Ready1 != nullptr && readyName == FName(item.Ready1)) ||
		(item.Ready2 != nullptr && readyName == FName(item.Ready2)) ||
		(item.Ready3 != nullptr && readyName == FName(item.Ready3));
}

bool BottomInventoryMatches(AActor *inventory, const FBottomItem &item)
{
	if (inventory == nullptr) return false;
	const FName name = inventory->GetClass()->TypeName;
	return (item.Inventory1 != nullptr && name == FName(item.Inventory1)) ||
		(item.Inventory2 != nullptr && name == FName(item.Inventory2)) ||
		(item.Inventory3 != nullptr && name == FName(item.Inventory3));
}

const std::array<FBottomItem, 16> &BottomItems()
{
	static constexpr FEmbeddedBottomImage Sword = BOTTOM_IMAGE(BottomItemSword);
	static constexpr FEmbeddedBottomImage Boomerang = BOTTOM_IMAGE(BottomItemBoomerang);
	static constexpr FEmbeddedBottomImage Bow = BOTTOM_IMAGE(BottomItemBow);
	static constexpr FEmbeddedBottomImage Bomb = BOTTOM_IMAGE(BottomItemBomb);
	static constexpr FEmbeddedBottomImage Candle = BOTTOM_IMAGE(BottomItemCandle);
	static constexpr FEmbeddedBottomImage Whistle = BOTTOM_IMAGE(BottomItemWhistle);
	static constexpr FEmbeddedBottomImage Wand = BOTTOM_IMAGE(BottomItemWand);
	static constexpr FEmbeddedBottomImage Potion = BOTTOM_IMAGE(BottomItemPotion);
	static constexpr FEmbeddedBottomImage Meat = BOTTOM_IMAGE(BottomItemMeat);
	static constexpr FEmbeddedBottomImage Ladder = BOTTOM_IMAGE(BottomItemLadder);
	static constexpr FEmbeddedBottomImage Raft = BOTTOM_IMAGE(BottomItemRaft);
	static constexpr FEmbeddedBottomImage Book = BOTTOM_IMAGE(BottomItemBook);
	static constexpr FEmbeddedBottomImage Ring = BOTTOM_IMAGE(BottomItemRing);
	static constexpr FEmbeddedBottomImage Bracelet = BOTTOM_IMAGE(BottomItemBracelet);
	static constexpr FEmbeddedBottomImage LionKey = BOTTOM_IMAGE(BottomItemLionKey);
	static constexpr FEmbeddedBottomImage Triforce = BOTTOM_IMAGE(BottomItemTriforce);
	static const std::array<FBottomItem, 16> Items = {{
		{ &Sword, "ZeldaSwordWood", "ZeldaSwordSilver", "ZeldaSwordMaster",
			"ZeldaSwordWood", "ZeldaSwordSilver", "ZeldaSwordMaster" },
		{ &Boomerang, "Boomerang", "BoomerangBlue", nullptr,
			"Boomerang", "BoomerangBlue", nullptr },
		{ &Bow, "Bow", "ZeldaSilverArrow", nullptr, "Bow", nullptr, nullptr },
		{ &Bomb, "ZeldaBomb", "ZeldaInfiniteBomb", "BombAmmo",
			"ZeldaBomb", "ZeldaInfiniteBomb", nullptr },
		{ &Candle, "CandleRed", "CandleBlue", nullptr,
			"CandleRed", "CandleBlue", nullptr },
		{ &Whistle, "ZeldaWhistle", nullptr, nullptr, "ZeldaWhistle", nullptr, nullptr },
		{ &Wand, "ZeldaWand", nullptr, nullptr, "ZeldaWand", nullptr, nullptr },
		{ &Potion, "PotAmmo", "ZeldaUniquePotion", "ZeldaLetter",
			"ZeldaPotion", nullptr, nullptr },
		{ &Meat, "ZeldaMeat", nullptr, nullptr, "ZeldaMeat", nullptr, nullptr },
		{ &Ladder, "ZeldaLadder", nullptr, nullptr, nullptr, nullptr, nullptr },
		{ &Raft, "ZeldaRaft", nullptr, nullptr, nullptr, nullptr, nullptr },
		{ &Book, "ZeldaBook", nullptr, nullptr, nullptr, nullptr, nullptr },
		{ &Ring, "ZeldaRingBlue", "ZeldaRingRed", nullptr, nullptr, nullptr, nullptr },
		{ &Bracelet, "ZeldaPowerBracelet", nullptr, nullptr, nullptr, nullptr, nullptr },
		{ &LionKey, "ZeldaLionKey", nullptr, nullptr, nullptr, nullptr, nullptr },
		{ &Triforce, "ZeldaTriforce", nullptr, nullptr, nullptr, nullptr, nullptr },
	}};
	return Items;
}

AActor *BottomFindInventory(AActor *owner, const char *first,
	const char *second = nullptr, const char *third = nullptr)
{
	if (owner == nullptr) return nullptr;
	for (const char *name : { first, second, third })
	{
		if (name == nullptr) continue;
		AActor *item = owner->FindInventory(FName(name), false);
		if (item != nullptr && item->IntVar(NAME_Amount) > 0) return item;
	}
	return nullptr;
}

bool BottomSelectItem(unsigned index)
{
	const auto &items = BottomItems();
	if (index >= items.size()) return false;
	player_t &player = players[consoleplayer];
	AActor *owner = player.mo;
	if (owner == nullptr) return false;
	const FBottomItem &item = items[index];
	AActor *weapon = BottomFindInventory(owner, item.Ready1, item.Ready2, item.Ready3);
	if (weapon != nullptr)
	{
		player.PendingWeapon = weapon;
		return true;
	}
	AActor *inventory = BottomFindInventory(owner,
		item.Inventory1, item.Inventory2, item.Inventory3);
	if (inventory == nullptr) return false;
	owner->PointerVar<AActor>(NAME_InvSel) = inventory;
	return true;
}

void DrawBottomItems(unsigned char *framebuffer, AActor *owner, AActor *readyWeapon)
{
	const auto &items = BottomItems();
	AActor *selectedInventory = owner != nullptr ?
		owner->PointerVar<AActor>(NAME_InvSel) : nullptr;

	constexpr int GridX = 34;
	constexpr int GridY = 38;
	constexpr int CellWidth = 49;
	constexpr int CellHeight = 41;
	for (unsigned index = 0; index < items.size(); ++index)
	{
		const FBottomItem &item = items[index];
		const int cellX = GridX + static_cast<int>(index % 4u) * CellWidth;
		const int cellY = GridY + static_cast<int>(index / 4u) * CellHeight;
		const bool available = BottomHasAnyInventory(owner, item.Inventory1,
			item.Inventory2, item.Inventory3);
		const bool selected = available && (BottomReadyWeaponMatches(readyWeapon, item) ||
			BottomInventoryMatches(selectedInventory, item));
		if (available)
		{
			if (selected)
			{
				DrawBottomRoundedRectOutline(framebuffer, cellX + 3, cellY,
					37, 35, 5, 2, OverlayIvory);
			}
			DrawEmbeddedBottomImage(framebuffer,
				cellX + (42 - static_cast<int>(item.Image->Width)) / 2,
				cellY + (35 - static_cast<int>(item.Image->Height)) / 2,
				*item.Image);
		}
	}
}

void DrawBottomHeartsAndCounters(unsigned char *framebuffer, AActor *owner)
{
	static constexpr FEmbeddedBottomImage HeartEmpty = BOTTOM_IMAGE(BottomHeartEmpty);
	static constexpr FEmbeddedBottomImage HeartQuarter = BOTTOM_IMAGE(BottomHeartQuarter);
	static constexpr FEmbeddedBottomImage HeartHalf = BOTTOM_IMAGE(BottomHeartHalf);
	static constexpr FEmbeddedBottomImage HeartThreeQuarter = BOTTOM_IMAGE(BottomHeartThreeQuarter);
	static constexpr FEmbeddedBottomImage HeartFull = BOTTOM_IMAGE(BottomHeartFull);
	static constexpr FEmbeddedBottomImage CounterRupee = BOTTOM_IMAGE(BottomCounterRupee);
	static constexpr FEmbeddedBottomImage CounterKey = BOTTOM_IMAGE(BottomCounterKey);
	static constexpr FEmbeddedBottomImage CounterBomb = BOTTOM_IMAGE(BottomCounterBomb);

	if (owner == nullptr) return;
	const int maximumHealth = std::max(8, owner->GetMaxHealth());
	int remainingHealth = std::max(0, owner->health);
	const unsigned hearts = std::min(20u, static_cast<unsigned>((maximumHealth + 7) / 8));
	constexpr int SideX = 242;
	constexpr int HeartSideX = 239;
	constexpr int SideWidth = 64;
	const unsigned columns = hearts <= 6u ? std::min(3u, std::max(1u, hearts)) :
		(hearts <= 12u ? std::min(4u, hearts) : std::min(5u, hearts));
	const int heartWidth = hearts <= 6u ? 14 : (hearts <= 12u ? 11 : 9);
	const int heartHeight = hearts <= 6u ? 16 : (hearts <= 12u ? 12 : 9);
	const int heartGap = hearts <= 6u ? 4 : 3;
	const int heartRowGap = hearts <= 6u ? 4 : (hearts <= 12u ? 3 : 2);
	const int rowWidth = static_cast<int>(columns) * heartWidth +
		(static_cast<int>(columns) - 1) * heartGap;
	const int heartStartX = HeartSideX + (SideWidth - rowWidth) / 2;
	constexpr int HeartStartY = 37;
	for (unsigned index = 0; index < hearts; ++index)
	{
		const int points = std::min(8, remainingHealth);
		remainingHealth -= points;
		const FEmbeddedBottomImage *heart = &HeartEmpty;
		if (points >= 7) heart = &HeartFull;
		else if (points >= 5) heart = &HeartThreeQuarter;
		else if (points >= 3) heart = &HeartHalf;
		else if (points >= 1) heart = &HeartQuarter;
		DrawEmbeddedBottomImageSized(framebuffer,
			heartStartX + static_cast<int>(index % columns) * (heartWidth + heartGap),
			HeartStartY + static_cast<int>(index / columns) * (heartHeight + heartRowGap),
			*heart, heartWidth, heartHeight);
	}
	const int heartRows = static_cast<int>((hearts + columns - 1u) / columns);
	const int heartsBottom = HeartStartY + heartRows * heartHeight +
		std::max(0, heartRows - 1) * heartRowGap;
	const int faceY = std::max(76, heartsBottom + 6);

	FGameTexture *face = StatusBar != nullptr && StatusBar->CPlayer != nullptr &&
		StatusBar->CPlayer->mo != nullptr ?
		StatusBar->mugshot.GetFace(StatusBar->CPlayer, "STF", 5) : nullptr;
	if (face != nullptr)
	{
		FString faceName = face->GetName();
		if (BottomInventoryAmount(owner, "ZeldaRingRed") > 0)
			faceName.Substitute("STF", "RRN");
		else if (BottomInventoryAmount(owner, "ZeldaRingBlue") > 0)
			faceName.Substitute("STF", "BRN");
		const FTextureID faceTexture = TexMan.CheckForTexture(faceName.GetChars(),
			ETextureType::Any, FTextureManager::TEXMAN_TryAny);
		if (faceTexture.isValid()) face = TexMan.GetGameTexture(faceTexture, true);
		constexpr int FaceWidth = 46;
		constexpr int FaceHeight = 55;
		DrawBottomGameTextureSized(framebuffer,
			SideX + (SideWidth - FaceWidth) / 2 - 3, faceY,
			face, FaceWidth, FaceHeight);
	}

	struct FCounter
	{
		const FEmbeddedBottomImage *Image;
		const char *Inventory;
		int Y;
	};
	const int counterStartY = std::max(157, faceY + 69);
	const FCounter Counters[] = {
		{ &CounterRupee, "ZeldaRupee", counterStartY },
		{ &CounterKey, "ZeldaKey", counterStartY + 18 },
		{ &CounterBomb, "BombAmmo", counterStartY + 36 },
	};
	for (const auto &counter : Counters)
	{
		// v0.27 grows the complete rupee/key/bomb cluster by another 15%.
		const int iconWidth = std::max(1,
			static_cast<int>(counter.Image->Width * 132u / 100u));
		const int iconHeight = std::max(1,
			static_cast<int>(counter.Image->Height * 132u / 100u));
		constexpr int textWidth = 3 * 8;
		constexpr int CounterGap = 3;
		const int counterX = SideX + (SideWidth - iconWidth - CounterGap - textWidth) / 2 - 5;
		DrawEmbeddedBottomImageSized(framebuffer, counterX, counter.Y,
			*counter.Image, iconWidth, iconHeight);
		char amount[8] = {};
		if (std::strcmp(counter.Inventory, "BombAmmo") == 0 &&
			BottomInventoryAmount(owner, "ZeldaInfiniteBomb") > 0)
		{
			std::snprintf(amount, sizeof(amount), "INF");
		}
		else
		{
			std::snprintf(amount, sizeof(amount), "%03d",
				std::clamp(BottomInventoryAmount(owner, counter.Inventory), 0, 999));
		}
		OverlayTextSized(framebuffer, counterX + iconWidth + CounterGap,
			counter.Y + 1, amount, 7, 9, 8, OverlayIvory);
	}
}

void DrawBottomTabs(unsigned char *framebuffer)
{
	// Keep the simple v0.28 tab treatment, but use a slightly smaller clean
	// pixel face. The blue frame line remains visible behind the two labels and
	// the selected page gets its own underline instead of a boxed button.
	constexpr int MapLeft = 31;
	constexpr int ItemsLeft = 133;
	constexpr int TabWidth = 100;
	OverlayRect(framebuffer, 16, 202, 288, 36, OverlayInk);
	OverlayRect(framebuffer, 16, 219, 288, 2, OverlayBlue);
	constexpr int GlyphWidth = 7;
	constexpr int GlyphHeight = 10;
	constexpr int Advance = 8;
	auto drawTab = [&](int left, const char *label, bool selected)
	{
		const int length = static_cast<int>(std::strlen(label));
		const int textWidth = length > 0 ? (length - 1) * Advance + GlyphWidth : 0;
		const int textX = left + (TabWidth - textWidth) / 2;
		OverlayRect(framebuffer, textX - 5, 207, textWidth + 10, 17, OverlayInk);
		OverlayTextSized(framebuffer, textX, 209, label,
			GlyphWidth, GlyphHeight, Advance, OverlayIvory);
		if (selected)
			OverlayRect(framebuffer, left + 22, 232, TabWidth - 44, 2, OverlayBlue);
	};
	drawTab(MapLeft, "MAP", BottomGameplayTab == EBottomGameplayTab::Map);
	drawTab(ItemsLeft, "ITEMS", BottomGameplayTab == EBottomGameplayTab::Items);
}

void DrawBottomGameplay(unsigned char *framebuffer)
{
	static constexpr FEmbeddedBottomImage Frame = BOTTOM_IMAGE(BottomGameFrame);
	DrawEmbeddedBottomImage(framebuffer, 0, 0, Frame);
	AActor *owner = players[consoleplayer].mo;
	if (BottomGameplayTab == EBottomGameplayTab::Map) DrawBottomAutomap(framebuffer);
	else DrawBottomItems(framebuffer, owner, players[consoleplayer].ReadyWeapon);
	DrawBottomHeartsAndCounters(framebuffer, owner);
	DrawBottomTabs(framebuffer);
}

#undef BOTTOM_IMAGE

void OverlayFrame(unsigned char *framebuffer, int x, int y, int width, int height,
	int thickness, FOverlayColor color)
{
	OverlayRect(framebuffer, x, y, width, thickness, color);
	OverlayRect(framebuffer, x, y + height - thickness, width, thickness, color);
	OverlayRect(framebuffer, x, y, thickness, height, color);
	OverlayRect(framebuffer, x + width - thickness, y, thickness, height, color);
}

void OverlayText(unsigned char *framebuffer, int x, int y, const char *text, int scale,
	FOverlayColor color)
{
	if (text == nullptr || scale <= 0) return;
	for (; *text != '\0'; ++text, x += 6 * scale)
	{
		const uint8_t *rows = LoadingFindGlyph(*text);
		if (rows == nullptr) continue;
		for (int row = 0; row < 7; ++row)
		{
			for (int column = 0; column < 5; ++column)
			{
				if ((rows[row] & (1u << (4 - column))) != 0)
				{
					OverlayRect(framebuffer, x + column * scale, y + row * scale,
						scale, scale, color);
				}
			}
		}
	}
}

void OverlayTextSized(unsigned char *framebuffer, int x, int y, const char *text,
	int glyphWidth, int glyphHeight, int advance, FOverlayColor color)
{
	if (text == nullptr || glyphWidth <= 0 || glyphHeight <= 0 || advance <= 0) return;
	for (; *text != '\0'; ++text, x += advance)
	{
		const uint8_t *rows = LoadingFindGlyph(*text);
		if (rows == nullptr) continue;
		for (int targetY = 0; targetY < glyphHeight; ++targetY)
		{
			const int sourceY = targetY * 7 / glyphHeight;
			for (int targetX = 0; targetX < glyphWidth; ++targetX)
			{
				const int sourceX = targetX * 5 / glyphWidth;
				if ((rows[sourceY] & (1u << (4 - sourceX))) != 0)
					OverlayPutPixel(framebuffer, x + targetX, y + targetY, color);
			}
		}
	}
}

void OverlayCenteredText(unsigned char *framebuffer, int x, int width, int y,
	const char *text, int scale, FOverlayColor color)
{
	OverlayText(framebuffer, x + (width - LoadingTextWidth(text, scale)) / 2,
		y, text, scale, color);
}

void OverlayProgressBar(unsigned char *framebuffer, int x, int y, int width,
	unsigned value, unsigned maximum, FOverlayColor fill)
{
	OverlayRect(framebuffer, x, y, width, 7, OverlayInkSoft);
	OverlayRect(framebuffer, x + 2, y + 2, width - 4, 3, OverlayParchment);
	if (maximum != 0)
	{
		const unsigned bounded = std::min(value, maximum);
		const int fillWidth = static_cast<int>((width - 4) * bounded / maximum);
		OverlayRect(framebuffer, x + 2, y + 2, fillWidth, 3, fill);
	}
}

const char *DiagnosticProgressTitle()
{
	switch (DiagnosticProgressMode)
	{
	case EDiagnosticDumpMode::Quick: return "QUICK DUMP";
	case EDiagnosticDumpMode::Full: return "FULL DUMP";
	case EDiagnosticDumpMode::Clean: return "CLEAN DUMPS";
	default: return "PLEASE WAIT";
	}
}

void DrawDiagnosticProgress(unsigned char *framebuffer)
{
	OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight,
		OverlayParchment);
	OverlayRect(framebuffer, 0, 29, 320, 2, OverlayBlue);
	OverlayRect(framebuffer, 0, 209, 320, 2, OverlayBlue);

	OverlayCenteredText(framebuffer, 0, 320, 55, DiagnosticProgressTitle(), 3,
		OverlayIvory);
	OverlayCenteredText(framebuffer, 0, 320, 86,
		DiagnosticProgressStage[0] != '\0' ? DiagnosticProgressStage : "PREPARING",
		1, OverlayIvory);

	constexpr int BarX = 26;
	constexpr int BarY = 119;
	constexpr int BarWidth = 268;
	OverlayFrame(framebuffer, BarX, BarY, BarWidth, 15, 2, OverlayBlue);
	OverlayRect(framebuffer, BarX + 3, BarY + 3, BarWidth - 6, 9, OverlayInk);
	unsigned percent = 0;
	if (DiagnosticProgressMaximum != 0)
	{
		percent = static_cast<unsigned>(std::min<uint64_t>(100,
			DiagnosticProgressValue * 100 / DiagnosticProgressMaximum));
		OverlayRect(framebuffer, BarX + 3, BarY + 3,
			static_cast<int>((BarWidth - 6) * percent / 100u), 9, OverlayBlue);
	}
	char progress[40] = {};
	if (DiagnosticProgressMode == EDiagnosticDumpMode::Full)
	{
		std::snprintf(progress, sizeof(progress), "%u%%  %llu/%llu MB", percent,
			static_cast<unsigned long long>(DiagnosticProgressValue / (1024u * 1024u)),
			static_cast<unsigned long long>(DiagnosticProgressMaximum / (1024u * 1024u)));
	}
	else if (DiagnosticProgressMode == EDiagnosticDumpMode::Clean)
	{
		std::snprintf(progress, sizeof(progress), "%u%%  %llu/%llu", percent,
			static_cast<unsigned long long>(DiagnosticProgressValue),
			static_cast<unsigned long long>(DiagnosticProgressMaximum));
	}
	else
	{
		std::snprintf(progress, sizeof(progress), "%u%%", percent);
	}
	OverlayCenteredText(framebuffer, 0, 320, 148, progress, 2, OverlayIvory);
	OverlayCenteredText(framebuffer, 0, 320, 181, "DO NOT POWER OFF", 1, OverlayIvory);
}

void SetOverlayNotification(const char *message, uint64_t durationMilliseconds =
	OverlayNotificationMilliseconds)
{
	std::snprintf(OverlayNotification, sizeof(OverlayNotification), "%s",
		message != nullptr ? message : "");
	OverlayNotificationUntilMilliseconds = osGetTime() + durationMilliseconds;
}

void DrawDeveloperOverlay(unsigned char *framebuffer)
{
	OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight, OverlayInk);
	OverlayText(framebuffer, 8, 7, "LEGEND OF DOOM", 1, OverlayIvory);
	char build[16] = {};
	std::snprintf(build, sizeof(build), "%.12s", LOD3DS_BUILD_ID);
	OverlayText(framebuffer, 188, 7, build, 1, OverlayIvory);
	char version[16] = {};
	std::snprintf(version, sizeof(version), "V%s", LOD3DS_PORT_VERSION);
	OverlayText(framebuffer, 278, 7, version, 1, OverlayIvory);
	OverlayRect(framebuffer, 0, 29, BottomScreenWidth, 2, OverlayBlue);

	const double frameMilliseconds = std::clamp(LastFrameMilliseconds, 0.0, 999.9);
	const double fps = frameMilliseconds > 0.0 ? 1000.0 / frameMilliseconds : LastCount;
	char fpsText[16] = {};
	char frameText[24] = {};
	std::snprintf(fpsText, sizeof(fpsText), "%.1f", std::clamp(fps, 0.0, 999.9));
	std::snprintf(frameText, sizeof(frameText), "FRAME %.1fMS", frameMilliseconds);
	OverlayFrame(framebuffer, 8, 38, 104, 89, 2, OverlayBlue);
	OverlayText(framebuffer, 15, 44, "PERFORMANCE", 1, OverlayIvory);
	OverlayCenteredText(framebuffer, 8, 104, 62, fpsText, 3, OverlayIvory);
	OverlayCenteredText(framebuffer, 8, 104, 88, "FPS", 1, OverlayIvory);
	OverlayCenteredText(framebuffer, 8, 104, 105, frameText, 1, OverlayIvory);

	OverlayFrame(framebuffer, 119, 38, 193, 89, 2, OverlayBlue);
	OverlayText(framebuffer, 126, 44, "MEMORY", 1, OverlayIvory);
	bool isNew3DS = false;
	APT_CheckNew3DS(&isNew3DS);
	const struct mallinfo heap = mallinfo();
	char heapText[24] = {};
	char linearText[24] = {};
	std::snprintf(heapText, sizeof(heapText), "HEAP FREE %luKB",
		static_cast<unsigned long>(heap.fordblks / 1024u));
	std::snprintf(linearText, sizeof(linearText), "LINEAR FREE %luKB",
		static_cast<unsigned long>(linearSpaceFree() / 1024u));
	OverlayText(framebuffer, 126, 58, heapText, 1, OverlayIvory);
	OverlayProgressBar(framebuffer, 126, 70, 178,
		static_cast<unsigned>(heap.fordblks),
		static_cast<unsigned>(std::max<size_t>(1u, heap.arena)), OverlayBlue);
	OverlayText(framebuffer, 126, 82, linearText, 1, OverlayIvory);
	OverlayProgressBar(framebuffer, 126, 94, 178,
		static_cast<unsigned>(linearSpaceFree()),
		static_cast<unsigned>(std::max<size_t>(1, envGetLinearHeapSize())), OverlayBlue);
	char render[40] = {};
	std::snprintf(render, sizeof(render), "CPU0+2 %dX%d PICA",
		I_3DSGameplayResolutionWidth(), I_3DSGameplayResolutionHeight());
	OverlayText(framebuffer, 126, 109, render, 1, OverlayIvory);

	OverlayFrame(framebuffer, 8, 138, 304, 33, 2, OverlayBlue);
	OverlayText(framebuffer, 15, 145, "STATUS", 1, OverlayIvory);
	OverlayText(framebuffer, 67, 145, OverlayAudioReady ? "AUDIO READY" : "AUDIO OFF", 1,
		OverlayGreen);
	OverlayText(framebuffer, 146, 145, isNew3DS ? "NEW 3DS 804MHZ" : "OLD 3DS",
		1, OverlayIvory);
	OverlayCenteredText(framebuffer, 8, 304, 158,
		"QUICK DUMP EXCLUDES 136 MB MEMORY IMAGE", 1, OverlayIvory);

	struct FButton { int X; const char *Title; const char *Chord; };
	static constexpr FButton Buttons[] = {
		{ 8, "QUICK DUMP", "L+R+A" },
		{ 112, "FULL MEMORY", "L+R+X" },
		{ 216, "CLEAN DUMPS", "L+R+Y" },
	};
	for (const auto &button : Buttons)
	{
		OverlayFrame(framebuffer, button.X, 181, 96, 50, 2, OverlayBlue);
		OverlayCenteredText(framebuffer, button.X, 96, 191, button.Title, 1, OverlayIvory);
		OverlayCenteredText(framebuffer, button.X, 96, 211, button.Chord, 1, OverlayBlue);
	}
}

void AddNativeMenuTouchRow(int top, int bottom, int item)
{
	if (NativeMenuTouchRowCount >= NativeMenuTouchRows.size()) return;
	NativeMenuTouchRows[NativeMenuTouchRowCount++] = { top, bottom, item };
}

void DrawNativePixelListMenu(unsigned char *framebuffer, DListMenuDescriptor *descriptor,
	const char *const *labels, const int *items, unsigned count, int firstY,
	const char *title = nullptr)
{
	NativeMenuCustomList = true;
	if (title != nullptr)
	{
		OverlayCenteredText(framebuffer, 0, BottomScreenWidth, 17, title, 2, OverlayRed);
	}
	FGameTexture *selector = descriptor->mSelector.isValid() ?
		TexMan.GetGameTexture(descriptor->mSelector, true) : nullptr;
	for (unsigned row = 0; row < count; ++row)
	{
		const int y = firstY + static_cast<int>(row) * 25;
		const int width = LoadingTextWidth(labels[row], 2);
		const int x = (static_cast<int>(BottomScreenWidth) - width) / 2;
		const bool selected = descriptor->mSelectedItem == items[row];
		if (selected)
		{
			if (selector != nullptr)
				DrawBottomGameTexture(framebuffer, x - 37, y - 7, selector, 30, 28);
			else
				OverlayText(framebuffer, x - 18, y, ">", 2, OverlayGold);
		}
		OverlayText(framebuffer, x, y, labels[row], 2, OverlayIvory);
		AddNativeMenuTouchRow(y - 5, y + 20, items[row]);
	}
}

bool DrawNativePixelMenu(unsigned char *framebuffer)
{
	if (CurrentMenu == nullptr) return false;
	if (CurrentMenu->IsKindOf("ListMenu"))
	{
		DListMenuDescriptor *descriptor =
			CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc"));
		if (descriptor == nullptr) return false;
		const FName name = descriptor->mMenuName;
		if (name == FName("MainMenu"))
		{
			static constexpr const char *Labels[] = {
				"NEW GAME", "OPTIONS", "LOAD GAME", "SAVE GAME", "QUIT GAME" };
			static constexpr int Items[] = { 0, 1, 2, 3, 4 };
			DrawNativePixelListMenu(framebuffer, descriptor, Labels, Items, 5, 61);
			return true;
		}
		if (name == FName("LegendPauseMenu"))
		{
			static constexpr const char *Labels[] = { "RESUME GAME", "NEW GAME", "OPTIONS",
				"LOAD GAME", "SAVE GAME", "QUIT GAME" };
			static constexpr int Items[] = { 0, 1, 2, 3, 4, 5 };
			DrawNativePixelListMenu(framebuffer, descriptor, Labels, Items, 6, 43);
			return true;
		}
		if (name == FName("SkillMenu"))
		{
			static constexpr const char *Labels[] = {
				"EASIEST", "EASY", "NORMAL", "HARD", "IMPOSSIBLE" };
			static constexpr int Items[] = { 1, 2, 3, 4, 5 };
			DrawNativePixelListMenu(framebuffer, descriptor, Labels, Items, 5, 64,
				"CHOOSE SKILL LEVEL");
			return true;
		}
		return false;
	}
	if (!CurrentMenu->IsKindOf("OptionMenu")) return false;
	DOptionMenuDescriptor *descriptor =
		CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
	if (descriptor == nullptr || descriptor->mMenuName == FName("LegendSoundOptions"))
		return false;

	const char *title = nullptr;
	const char *labels[4] = {};
	const char *values[4] = {};
	unsigned count = 0;
	if (descriptor->mMenuName == FName("LegendOptionsMenu"))
	{
		title = "OPTIONS";
		labels[0] = "VOLUME"; labels[1] = "DISPLAY"; labels[2] = "DEVELOPER";
		count = 3;
	}
	else if (descriptor->mMenuName == FName("LegendDisplayOptions"))
	{
		title = "DISPLAY";
		labels[0] = "RENDER"; labels[1] = "TOP HUD";
		labels[2] = "AIM CROSS"; labels[3] = "MAP COLLISIONS";
		values[0] = lod3ds_render_scale == 5 ? "0.5" :
			(lod3ds_render_scale == 10 ? "1.0" : "0.8");
		values[1] = lod3ds_top_hud ? "ON" : "OFF";
		values[2] = crosshairon ? "ON" : "OFF";
		values[3] = lod3ds_map_collisions ? "ON" : "OFF";
		count = 4;
	}
	else if (descriptor->mMenuName == FName("LegendDeveloperOptions"))
	{
		title = "DEVELOPER";
		labels[0] = "SHOW FPS"; labels[1] = "QUICK DUMP";
		labels[2] = "FULL DUMP"; labels[3] = "OVERLAY WITH SELECT";
		values[0] = vid_fps ? "ON" : "OFF";
		values[3] = lod3ds_select_overlay ? "ON" : "OFF";
		count = 4;
	}
	else return false;

	NativeMenuCustomOption = true;
	OverlayCenteredText(framebuffer, 0, BottomScreenWidth, 16, title, 3, OverlayRed);
	for (unsigned row = 0; row < count; ++row)
	{
		const int y = 68 + static_cast<int>(row) * 31;
		const bool selected = descriptor->mSelectedItem == static_cast<int>(row);
		if (selected) OverlayText(framebuffer, 27, y, ">", 2, OverlayGold);
		const FOverlayColor labelColor = selected ? OverlayGold : OverlayIvory;
		if (values[row] != nullptr && LoadingTextWidth(labels[row], 2) > 180)
			OverlayTextSized(framebuffer, 45, y + 2, labels[row], 7, 10, 8, labelColor);
		else
			OverlayText(framebuffer, 45, y, labels[row], 2, labelColor);
		if (values[row] != nullptr)
		{
			OverlayText(framebuffer, 276 - LoadingTextWidth(values[row], 2), y,
				values[row], 2, OverlayIvory);
		}
		AddNativeMenuTouchRow(y - 6, y + 22, static_cast<int>(row));
	}
	return true;
}

constexpr unsigned NativeMenuBrightness = 71u;
constexpr float NativeMenuRequestedScale = 1.28f;
constexpr float NativeOptionTitleScale = 1.44f;

unsigned NativeMenuCurrentBrightness()
{
	return CurrentMenu != nullptr && CurrentMenu->DontDim ? 255u :
		NativeMenuBrightness;
}

unsigned char NativeMenuDimChannel(unsigned char value, unsigned brightness)
{
	return static_cast<unsigned char>((static_cast<unsigned>(value) *
		brightness + 127u) / 255u);
}

bool NativeSaveLoadMenuVisible()
{
	return CurrentMenu != nullptr && CurrentMenu->IsKindOf("LoadSaveMenu");
}

bool DrawNativeSaveLoadBottomFrame(unsigned char *framebuffer,
	const unsigned char *menuPixels, int pitchBytes, int width, int height)
{
	if (!NativeSaveLoadMenuVisible()) return false;
	(void)menuPixels;
	(void)pitchBytes;
	(void)width;
	(void)height;
	OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight, OverlayInk);
	NativeMenuCustomSave = true;

	// Save/Load owns the complete touch LCD. The title belongs exclusively to
	// the upper screen; this screen is a clean, roomy slot list.
	constexpr int SectionLeft = 12;
	constexpr int SectionTop = 7;
	constexpr int SectionWidth = 296;
	constexpr int SectionHeight = 226;
	constexpr int FirstRowY = 14;
	constexpr int RowHeight = 21;
	constexpr int VisibleRows = 10;
	OverlayFrame(framebuffer, SectionLeft, SectionTop, SectionWidth,
		SectionHeight, 2, OverlayIvory);

	const int saveCount = static_cast<int>(savegameManager.SavegameCount());
	const int selected = CurrentMenu != nullptr
		? CurrentMenu->IntVar(FName("Selected")) : -1;
	int topItem = CurrentMenu != nullptr
		? CurrentMenu->IntVar(FName("TopItem")) : 0;
	topItem = std::clamp(topItem, 0, std::max(0, saveCount - 1));
	if (saveCount == 0)
	{
		OverlayCenteredText(framebuffer, SectionLeft, SectionWidth,
			116, "NO SAVE GAMES", 2, OverlayIvory);
		return true;
	}

	for (int row = 0; row < VisibleRows && topItem + row < saveCount; ++row)
	{
		const int item = topItem + row;
		const int rowY = FirstRowY + row * RowHeight;
		if (item == selected)
		{
			OverlayRect(framebuffer, SectionLeft + 3, rowY - 4,
				SectionWidth - 6, RowHeight - 2, OverlayBlue);
		}
		FSaveGameNode *node = savegameManager.GetSavegame(item);
		char label[80] = {};
		std::snprintf(label, sizeof(label), "%s",
			node != nullptr ? node->SaveTitle.GetChars() : "");
		size_t length = std::strlen(label);
		constexpr int Advance = 8;
		const size_t maximumCharacters = (SectionWidth - 20) / Advance;
		while (length > 3 && length > maximumCharacters)
		{
			label[--length] = '\0';
		}
		if (length + 3 < sizeof(label) && node != nullptr &&
			std::strlen(node->SaveTitle.GetChars()) > length)
		{
			label[length++] = '.';
			label[length++] = '.';
			label[length++] = '.';
			label[length] = '\0';
		}
		OverlayTextSized(framebuffer, SectionLeft + 9, rowY,
			label, 7, 10, Advance, OverlayIvory);
		AddNativeMenuTouchRow(rowY - 4, rowY + RowHeight - 4, item);
	}
	return true;
}

void DrawNativeMenuBottomFrame(unsigned char *framebuffer,
	const unsigned char *menuPixels, const unsigned char *basePixels,
	int pitchBytes, int width, int height)
{
	const unsigned brightness = NativeMenuCurrentBrightness();
	OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight, OverlayInk);
	NativeMenuTransformValid = false;
	NativeMenuTouchRowCount = 0;
	NativeMenuCustomList = false;
	NativeMenuCustomOption = false;
	NativeMenuCustomSave = false;
	if (DrawNativeSaveLoadBottomFrame(framebuffer, menuPixels,
		pitchBytes, width, height)) return;
	if (gamestate == GS_DEMOSCREEN)
	{
		// Preserve the two-screen title composition: the real menu controls sit
		// over the waterfall artwork instead of mirroring the upper title panel.
		DrawMenuBottomScreen(framebuffer, brightness);
	}
	// Route every menu through the engine renderer used by the approved Volume
	// screen. This keeps SmallFont, sliders, Link and spacing consistent instead
	// of mixing them with the former bespoke 5x7 menu renderer.

	auto isMenuPixel = [&](int sourceX, int sourceY)
	{
		const unsigned char *menu = menuPixels + sourceY * pitchBytes + sourceX * 4;
		const unsigned char *base = basePixels + sourceY * pitchBytes + sourceX * 4;
		const int difference =
			std::abs(static_cast<int>(menu[0]) - NativeMenuDimChannel(base[0], brightness)) +
			std::abs(static_cast<int>(menu[1]) - NativeMenuDimChannel(base[1], brightness)) +
			std::abs(static_cast<int>(menu[2]) - NativeMenuDimChannel(base[2], brightness));
		return difference > 12;
	};

	int minimumX = width;
	int minimumY = height;
	int maximumX = -1;
	int maximumY = -1;
	for (int sourceY = 0; sourceY < height; ++sourceY)
	{
		for (int sourceX = 0; sourceX < width; ++sourceX)
		{
			if (!isMenuPixel(sourceX, sourceY)) continue;
			minimumX = std::min(minimumX, sourceX);
			minimumY = std::min(minimumY, sourceY);
			maximumX = std::max(maximumX, sourceX);
			maximumY = std::max(maximumY, sourceY);
		}
	}
	if (maximumX < minimumX || maximumY < minimumY) return;

	const bool optionMenu = CurrentMenu != nullptr && CurrentMenu->IsKindOf("OptionMenu");
	bool stableListMenu = false;
	bool sliderOptionMenu = false;
	if (CurrentMenu != nullptr && CurrentMenu->IsKindOf("ListMenu"))
	{
		DListMenuDescriptor *descriptor =
			CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc"));
		if (descriptor != nullptr)
		{
			const FName name = descriptor->mMenuName;
			stableListMenu = name == FName("MainMenu") ||
				name == FName("LegendPauseMenu");
		}
	}
	if (optionMenu)
	{
		DOptionMenuDescriptor *descriptor =
			CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
		if (descriptor != nullptr)
		{
			const FName name = descriptor->mMenuName;
			sliderOptionMenu = name == FName("LegendControllerOptions") ||
				name == FName("LegendSoundOptions");
		}
	}

	// Main and pause previously jumped whenever the selector changed rows,
	// because its sprite altered the automatically detected vertical bounds.
	// Measure the stable text column for layout while still drawing the cursor
	// pixels that extend beyond those bounds.
	int layoutMinimumY = minimumY;
	int layoutMaximumY = maximumY;
	if (stableListMenu)
	{
		int textMinimumY = height;
		int textMaximumY = -1;
		const int textColumnLeft = width * 2 / 5;
		for (int sourceY = minimumY; sourceY <= maximumY; ++sourceY)
		{
			for (int sourceX = textColumnLeft; sourceX <= maximumX; ++sourceX)
			{
				if (!isMenuPixel(sourceX, sourceY)) continue;
				textMinimumY = std::min(textMinimumY, sourceY);
				textMaximumY = std::max(textMaximumY, sourceY);
			}
		}
		if (textMaximumY >= textMinimumY)
		{
			layoutMinimumY = textMinimumY;
			layoutMaximumY = textMaximumY;
		}
	}
	const int sourceWidth = maximumX - minimumX + 1;
	const int sourceHeight = layoutMaximumY - layoutMinimumY + 1;
	const float maximumTargetHeight = optionMenu ? 202.0f : 224.0f;
	const float scale = std::min({ NativeMenuRequestedScale,
		304.0f / sourceWidth, maximumTargetHeight / sourceHeight });
	const float targetWidth = sourceWidth * scale;
	const float targetHeight = sourceHeight * scale;
	const float horizontalOffset = stableListMenu ? 10.0f :
		(sliderOptionMenu ? 10.0f : 0.0f);
	const float targetLeft = (BottomScreenWidth - targetWidth) * 0.5f +
		horizontalOffset;
	const float targetTop = optionMenu ? 28.0f :
		(BottomScreenHeight - targetHeight) * 0.5f;

	int titleBottom = minimumY - 1;
	if (optionMenu)
	{
		bool titleStarted = false;
		int emptyRows = 0;
		for (int sourceY = minimumY; sourceY <= maximumY; ++sourceY)
		{
			bool occupied = false;
			for (int sourceX = minimumX; sourceX <= maximumX && !occupied; ++sourceX)
				occupied = isMenuPixel(sourceX, sourceY);
			if (occupied)
			{
				titleStarted = true;
				titleBottom = sourceY;
				emptyRows = 0;
			}
			else if (titleStarted && ++emptyRows >= 4)
			{
				break;
			}
		}
	}

	const int firstTargetX = std::max(0, static_cast<int>(std::floor(targetLeft)));
	const int firstTargetY = std::max(0, static_cast<int>(std::floor(
		targetTop + (minimumY - layoutMinimumY) * scale)));
	const int lastTargetX = std::min(static_cast<int>(BottomScreenWidth) - 1,
		static_cast<int>(std::ceil(targetLeft + targetWidth)) - 1);
	const int lastTargetY = std::min(static_cast<int>(BottomScreenHeight) - 1,
		static_cast<int>(std::ceil(targetTop +
			(maximumY - layoutMinimumY + 1) * scale)) - 1);
	for (int targetY = firstTargetY; targetY <= lastTargetY; ++targetY)
	{
		const int sourceY = std::clamp(layoutMinimumY + static_cast<int>(
			(targetY - targetTop) / scale), minimumY, maximumY);
		for (int targetX = firstTargetX; targetX <= lastTargetX; ++targetX)
		{
			const int sourceX = std::clamp(minimumX + static_cast<int>(
				(targetX - targetLeft) / scale), minimumX, maximumX);
			if ((optionMenu && sourceY <= titleBottom) || !isMenuPixel(sourceX, sourceY))
				continue;
			const unsigned char *menu = menuPixels + sourceY * pitchBytes + sourceX * 4;
			OverlayPutPixel(framebuffer, targetX, targetY,
				FOverlayColor{ menu[2], menu[1], menu[0] });
		}
	}

	if (optionMenu && titleBottom >= minimumY)
	{
		int titleLeft = maximumX;
		int titleRight = minimumX;
		for (int sourceY = minimumY; sourceY <= titleBottom; ++sourceY)
		{
			for (int sourceX = minimumX; sourceX <= maximumX; ++sourceX)
			{
				if (!isMenuPixel(sourceX, sourceY)) continue;
				titleLeft = std::min(titleLeft, sourceX);
				titleRight = std::max(titleRight, sourceX);
			}
		}
		const int titleWidth = titleRight - titleLeft + 1;
		const int titleHeight = titleBottom - minimumY + 1;
		const float titleScale = std::min(NativeOptionTitleScale, 304.0f / titleWidth);
		const float titleTargetLeft =
			(BottomScreenWidth - titleWidth * titleScale) * 0.5f;
		constexpr float TitleTargetTop = 18.0f;
		for (int targetY = static_cast<int>(TitleTargetTop);
			targetY < static_cast<int>(std::ceil(TitleTargetTop + titleHeight * titleScale));
			++targetY)
		{
			const int sourceY = std::clamp(minimumY + static_cast<int>(
				(targetY - TitleTargetTop) / titleScale), minimumY, titleBottom);
			for (int targetX = std::max(0, static_cast<int>(titleTargetLeft));
				targetX < std::min(static_cast<int>(BottomScreenWidth),
					static_cast<int>(std::ceil(titleTargetLeft + titleWidth * titleScale)));
				++targetX)
			{
				const int sourceX = std::clamp(titleLeft + static_cast<int>(
					(targetX - titleTargetLeft) / titleScale), titleLeft, titleRight);
				if (!isMenuPixel(sourceX, sourceY)) continue;
				const unsigned char *menu = menuPixels + sourceY * pitchBytes + sourceX * 4;
				OverlayPutPixel(framebuffer, targetX, targetY,
					FOverlayColor{ menu[2], menu[1], menu[0] });
			}
		}
	}

	NativeMenuTransformValid = true;
	NativeMenuSourceLeft = static_cast<float>(minimumX);
	NativeMenuSourceTop = static_cast<float>(layoutMinimumY);
	NativeMenuTargetLeft = targetLeft;
	NativeMenuTargetTop = targetTop;
	NativeMenuScale = scale;
}

void NativeTopBlendPixel(unsigned char *pixels, int pitchBytes, int width, int height,
	int x, int y, FOverlayColor color, unsigned alpha)
{
	if (x < 0 || x >= width || y < 0 || y >= height || alpha == 0) return;
	unsigned char *target = pixels + y * pitchBytes + x * 4;
	const unsigned source[] = { color.Blue, color.Green, color.Red };
	for (int channel = 0; channel < 3; ++channel)
	{
		target[channel] = static_cast<unsigned char>((source[channel] * alpha +
			target[channel] * (255u - alpha) + 127u) / 255u);
	}
	target[3] = 255u;
}

void NativeTopRect(unsigned char *pixels, int pitchBytes, int width, int height,
	int x, int y, int rectWidth, int rectHeight, FOverlayColor color, unsigned alpha)
{
	for (int py = y; py < y + rectHeight; ++py)
		for (int px = x; px < x + rectWidth; ++px)
			NativeTopBlendPixel(pixels, pitchBytes, width, height, px, py, color, alpha);
}

void NativeTopFontText(unsigned char *pixels, int pitchBytes, int width, int height,
	FFont *font, int x, int y, const char *text, int scale, FOverlayColor color)
{
	if (pixels == nullptr || font == nullptr || text == nullptr || scale <= 0) return;
	const int kerning = font->GetDefaultKerning();
	for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
		*cursor != '\0'; ++cursor)
	{
		int advance = font->GetSpaceWidth();
		FGameTexture *glyph = font->GetChar(*cursor, CR_UNTRANSLATED, &advance);
		if (glyph != nullptr && glyph->GetTexture() != nullptr)
		{
			FBitmap bitmap = glyph->GetTexture()->GetBgraBitmap(nullptr);
			if (bitmap.GetPixels() != nullptr)
			{
				for (int sourceY = 0; sourceY < bitmap.GetHeight(); ++sourceY)
				{
					for (int sourceX = 0; sourceX < bitmap.GetWidth(); ++sourceX)
					{
						const uint8_t alpha = bitmap.GetPixels()[
							sourceY * bitmap.GetPitch() + sourceX * 4 + 3];
						if (alpha < 8u) continue;
						for (int sy = 0; sy < scale; ++sy)
							for (int sx = 0; sx < scale; ++sx)
								NativeTopBlendPixel(pixels, pitchBytes, width, height,
									x + sourceX * scale + sx, y + sourceY * scale + sy,
									color, alpha);
					}
				}
			}
		}
		x += std::max(1, advance + kerning) * scale;
	}
}

void NativeTopCenteredFontText(unsigned char *pixels, int pitchBytes,
	int width, int height, FFont *font, int x, int areaWidth, int y,
	const char *text, int scale, FOverlayColor color)
{
	NativeTopFontText(pixels, pitchBytes, width, height, font,
		x + (areaWidth - NativeFontTextWidth(font, text, scale)) / 2,
		y, text, scale, color);
}

void NativeTopWrappedFontText(unsigned char *pixels, int pitchBytes,
	int width, int height, FFont *font, int x, int y, int maximumWidth,
	int maximumLines, const char *text, FOverlayColor color)
{
	if (font == nullptr || text == nullptr || maximumWidth <= 0 || maximumLines <= 0)
		return;
	char line[96] = {};
	int length = 0;
	int lines = 0;
	auto flush = [&]()
	{
		if (length <= 0 || lines >= maximumLines) return;
		line[length] = '\0';
		NativeTopFontText(pixels, pitchBytes, width, height, font,
			x, y + lines * (font->GetHeight() + 2), line, 1, color);
		length = 0;
		line[0] = '\0';
		++lines;
	};
	for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
		*cursor != '\0' && lines < maximumLines; ++cursor)
	{
		if (*cursor == '\n')
		{
			flush();
			continue;
		}
		if (*cursor < 32u || *cursor > 126u) continue;
		if (length >= static_cast<int>(sizeof(line)) - 2) flush();
		if (lines >= maximumLines) break;
		line[length] = static_cast<char>(*cursor);
		line[length + 1] = '\0';
		if (length > 0 && NativeFontTextWidth(font, line) > maximumWidth)
		{
			line[length] = '\0';
			flush();
			if (lines >= maximumLines) break;
			line[length] = static_cast<char>(*cursor);
			line[length + 1] = '\0';
		}
		++length;
	}
	flush();
}

void NativeTopScaledText(unsigned char *pixels, int pitchBytes, int width, int height,
	int x, int y, const char *text, int scale, FOverlayColor color)
{
	if (scale <= 0) return;
	for (; text != nullptr && *text != '\0'; ++text, x += 6 * scale)
	{
		const uint8_t *rows = LoadingFindGlyph(*text);
		if (rows == nullptr) continue;
		for (int row = 0; row < 7; ++row)
			for (int column = 0; column < 5; ++column)
				if ((rows[row] & (1u << (4 - column))) != 0)
					for (int sy = 0; sy < scale; ++sy)
						for (int sx = 0; sx < scale; ++sx)
							NativeTopBlendPixel(pixels, pitchBytes, width, height,
								x + column * scale + sx,
								y + row * scale + sy, color, 255u);
	}
}

void NativeTopText(unsigned char *pixels, int pitchBytes, int width, int height,
	int x, int y, const char *text, FOverlayColor color)
{
	NativeTopScaledText(pixels, pitchBytes, width, height,
		x, y, text, 1, color);
}

void NativeTopCenteredText(unsigned char *pixels, int pitchBytes,
	int width, int height, int x, int areaWidth, int y,
	const char *text, int scale, FOverlayColor color)
{
	NativeTopScaledText(pixels, pitchBytes, width, height,
		x + (areaWidth - LoadingTextWidth(text, scale)) / 2,
		y, text, scale, color);
}

void NativeTopGameTextureSized(unsigned char *pixels, int pitchBytes, int width, int height,
	FGameTexture *texture, int x, int y, int targetWidth, int targetHeight)
{
	if (texture == nullptr || texture->GetTexture() == nullptr ||
		targetWidth <= 0 || targetHeight <= 0) return;
	FBitmap bitmap = texture->GetTexture()->GetBgraBitmap(nullptr);
	const int sourceWidth = bitmap.GetWidth();
	const int sourceHeight = bitmap.GetHeight();
	if (sourceWidth <= 0 || sourceHeight <= 0 || bitmap.GetPixels() == nullptr) return;
	for (int targetY = 0; targetY < targetHeight; ++targetY)
	{
		const int sourceY = std::min(sourceHeight - 1,
			targetY * sourceHeight / targetHeight);
		for (int targetX = 0; targetX < targetWidth; ++targetX)
		{
			const int sourceX = std::min(sourceWidth - 1,
				targetX * sourceWidth / targetWidth);
			const uint8_t *source = bitmap.GetPixels() +
				sourceY * bitmap.GetPitch() + sourceX * 4;
			if (source[3] < 8u) continue;
			NativeTopBlendPixel(pixels, pitchBytes, width, height,
				x + targetX, y + targetY,
				FOverlayColor{ source[2], source[1], source[0] }, source[3]);
		}
	}
}

void NativeTopWrappedText(unsigned char *pixels, int pitchBytes, int width, int height,
	int x, int y, int maximumCharacters, int maximumLines,
	const char *text, FOverlayColor color)
{
	if (text == nullptr || maximumCharacters <= 0 || maximumLines <= 0) return;
	char line[64] = {};
	int length = 0;
	int lines = 0;
	auto flush = [&]()
	{
		if (lines >= maximumLines) return;
		line[length] = '\0';
		NativeTopText(pixels, pitchBytes, width, height, x, y + lines * 9, line, color);
		length = 0;
		line[0] = '\0';
		++lines;
	};
	for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
		*cursor != '\0' && lines < maximumLines; ++cursor)
	{
		if (*cursor == '\n')
		{
			flush();
			continue;
		}
		if (*cursor < 32u || *cursor > 126u) continue;
		if (length >= maximumCharacters || length >= static_cast<int>(sizeof(line)) - 1)
			flush();
		if (lines >= maximumLines) break;
		line[length++] = static_cast<char>(*cursor);
	}
	if (length > 0 && lines < maximumLines) flush();
}

void CopyNativeSaveTitle(unsigned char *pixels, const unsigned char *menuSource,
	const unsigned char *basePixels, int pitchBytes, int width, int height,
	unsigned brightness)
{
	int minimumX = width;
	int minimumY = height;
	int maximumX = -1;
	int maximumY = -1;
	const int searchBottom = std::min(height - 1, height / 5 - 1);
	for (int y = 0; y <= searchBottom; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			const unsigned char *menu = menuSource + y * pitchBytes + x * 4;
			const unsigned char *base = basePixels + y * pitchBytes + x * 4;
			const int difference =
				std::abs(static_cast<int>(menu[0]) - NativeMenuDimChannel(base[0], brightness)) +
				std::abs(static_cast<int>(menu[1]) - NativeMenuDimChannel(base[1], brightness)) +
				std::abs(static_cast<int>(menu[2]) - NativeMenuDimChannel(base[2], brightness));
			if (difference <= 12) continue;
			minimumX = std::min(minimumX, x);
			minimumY = std::min(minimumY, y);
			maximumX = std::max(maximumX, x);
			maximumY = std::max(maximumY, y);
		}
	}
	if (maximumX < minimumX || maximumY < minimumY) return;
	const int sourceWidth = maximumX - minimumX + 1;
	const int sourceHeight = maximumY - minimumY + 1;
	const float scale = std::min(1.5f, 300.0f / std::max(1, sourceWidth));
	const int targetWidth = std::max(1, static_cast<int>(sourceWidth * scale + 0.5f));
	const int targetHeight = std::max(1, static_cast<int>(sourceHeight * scale + 0.5f));
	const int targetLeft = (width - targetWidth) / 2;
	constexpr int TargetTop = 7;
	for (int y = 0; y < targetHeight; ++y)
	{
		const int sourceY = minimumY + static_cast<int>(y / scale);
		for (int x = 0; x < targetWidth; ++x)
		{
			const int sourceX = minimumX + static_cast<int>(x / scale);
			const unsigned char *menu = menuSource + sourceY * pitchBytes + sourceX * 4;
			const unsigned char *base = basePixels + sourceY * pitchBytes + sourceX * 4;
			const int difference =
				std::abs(static_cast<int>(menu[0]) - NativeMenuDimChannel(base[0], brightness)) +
				std::abs(static_cast<int>(menu[1]) - NativeMenuDimChannel(base[1], brightness)) +
				std::abs(static_cast<int>(menu[2]) - NativeMenuDimChannel(base[2], brightness));
			if (difference <= 12) continue;
			NativeTopBlendPixel(pixels, pitchBytes, width, height,
				targetLeft + x, TargetTop + y,
				FOverlayColor{ menu[2], menu[1], menu[0] }, 255u);
		}
	}
}

void ComposeNativeSaveLoadTop(unsigned char *pixels, const unsigned char *menuSource,
	const unsigned char *basePixels, int pitchBytes, int width, int height,
	unsigned brightness)
{
	(void)basePixels;
	(void)brightness;
	const bool saving = CurrentMenu != nullptr && CurrentMenu->IsKindOf("SaveMenu");
	NativeTopCenteredText(pixels, pitchBytes, width, height,
		0, width, 3, saving ? "SAVE GAME" : "LOAD GAME", 2, OverlayIvory);
	const int sourceLeft = width / 8;
	const int sourceTop = height / 5;
	const int sourceWidth = width * 3 / 10;
	const int sourceHeight = height * 3 / 8;
	const int targetWidth = std::min(200, width - 24);
	const int targetHeight = targetWidth * 3 / 4;
	const int targetLeft = (width - targetWidth) / 2;
	// v0.29: lift the preview eight pixels from the v0.28 placement.
	constexpr int TargetTop = 21;
	for (int y = 0; y < targetHeight; ++y)
	{
		const int sourceY = sourceTop + y * sourceHeight / targetHeight;
		for (int x = 0; x < targetWidth; ++x)
		{
			const int sourceX = sourceLeft + x * sourceWidth / targetWidth;
			const unsigned char *source = menuSource + sourceY * pitchBytes + sourceX * 4;
			NativeTopBlendPixel(pixels, pitchBytes, width, height,
				targetLeft + x, TargetTop + y,
				FOverlayColor{ source[2], source[1], source[0] }, 255u);
		}
	}

	constexpr int InfoTop = 177;
	const int InfoLeft = targetLeft;
	const int infoWidth = targetWidth;
	const int infoHeight = height - InfoTop - 7;
	// The scene remains visible through the information area. Only its thin
	// white frame is drawn, at exactly the same width as the preview above.
	NativeTopRect(pixels, pitchBytes, width, height,
		InfoLeft, InfoTop, infoWidth, 1, OverlayIvory, 255u);
	NativeTopRect(pixels, pitchBytes, width, height,
		InfoLeft, InfoTop + infoHeight - 1, infoWidth, 1, OverlayIvory, 255u);
	NativeTopRect(pixels, pitchBytes, width, height,
		InfoLeft, InfoTop, 1, infoHeight, OverlayIvory, 255u);
	NativeTopRect(pixels, pitchBytes, width, height,
		InfoLeft + infoWidth - 1, InfoTop, 1, infoHeight, OverlayIvory, 255u);

	FString information;
	const int selected = CurrentMenu != nullptr ? CurrentMenu->IntVar(FName("Selected")) : -1;
	if (selected >= 0 && static_cast<unsigned>(selected) < savegameManager.SavegameCount())
	{
		FSaveGameNode *node = savegameManager.GetSavegame(selected);
		if (node != nullptr) information = node->SaveTitle;
	}
	if (!information.IsEmpty())
	{
		char title[64] = {};
		std::snprintf(title, sizeof(title), "%s", information.GetChars());
		const size_t maximumCharacters = static_cast<size_t>((infoWidth - 12) / 6);
		if (std::strlen(title) > maximumCharacters && maximumCharacters > 3)
		{
			title[maximumCharacters - 3] = '.';
			title[maximumCharacters - 2] = '.';
			title[maximumCharacters - 1] = '.';
			title[maximumCharacters] = '\0';
		}
		NativeTopText(pixels, pitchBytes, width, height,
			InfoLeft + 6, InfoTop + 5, title, OverlayGold);
	}
	if (!savegameManager.SaveCommentString.IsEmpty())
		NativeTopWrappedText(pixels, pitchBytes, width, height,
			InfoLeft + 6, InfoTop + 16, (infoWidth - 12) / 6, 4,
			savegameManager.SaveCommentString.GetChars(), OverlayIvory);
}

void ComposeNativeMenuFps(unsigned char *pixels, int pitchBytes, int width, int height)
{
	if (!vid_fps) return;
	char label[32] = {};
	std::snprintf(label, sizeof(label), "FPS %llu  %.1fMS",
		static_cast<unsigned long long>(std::min<uint64_t>(LastCount, 999u)),
		std::clamp(LastFrameMilliseconds, 0.0, 999.9));
	const int boxWidth = LoadingTextWidth(label, 1) + 8;
	const int x = std::max(2, width - boxWidth - 4);
	NativeTopRect(pixels, pitchBytes, width, height, x, 4, boxWidth, 13,
		OverlayInk, 210u);
	NativeTopText(pixels, pitchBytes, width, height, x + 4, 7, label, OverlayIvory);
}

void ComposeNativeMenuTop(unsigned char *pixels, int pitchBytes, int width, int height,
	const unsigned char *menuSource = nullptr, const unsigned char *basePixels = nullptr)
{
	const unsigned brightness = NativeMenuCurrentBrightness();
	for (int y = 0; y < height; ++y)
	{
		unsigned char *row = pixels + y * pitchBytes;
		for (int x = 0; x < width; ++x)
		{
			row[x * 4 + 0] = NativeMenuDimChannel(row[x * 4 + 0], brightness);
			row[x * 4 + 1] = NativeMenuDimChannel(row[x * 4 + 1], brightness);
			row[x * 4 + 2] = NativeMenuDimChannel(row[x * 4 + 2], brightness);
		}
	}

	if (NativeSaveLoadMenuVisible() && menuSource != nullptr && basePixels != nullptr)
	{
		ComposeNativeSaveLoadTop(pixels, menuSource, basePixels,
			pitchBytes, width, height, brightness);
	}
	ComposeNativeMenuFps(pixels, pitchBytes, width, height);
}

EBottomPresentation DesiredBottomPresentation()
{
	if (DiagnosticProgressActive) return EBottomPresentation::Progress;
	if (menuactive != MENU_Off && CurrentMenu != nullptr)
		return EBottomPresentation::NativeMenu;
	if (DeveloperOverlayVisible && gamestate == GS_LEVEL)
		return EBottomPresentation::DeveloperOverlay;
	if (gamestate == GS_LEVEL) return EBottomPresentation::Gameplay;
	if (gamestate != GS_DEMOSCREEN || MenuStoryPage)
	{
		return EBottomPresentation::Blank;
	}
	return menuactive == MENU_Off ? EBottomPresentation::Menu :
		EBottomPresentation::MenuDimmed;
}

void DrawBottomOverlay(bool force)
{
	const uint64_t now = osGetTime();
	const EBottomPresentation desired = DesiredBottomPresentation();
	if (!force && BottomPresentation == desired &&
		now - OverlayLastDrawMilliseconds < OverlayRefreshMilliseconds) return;

	u16 physicalWidth = 0;
	u16 physicalHeight = 0;
	unsigned char *framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT,
		&physicalWidth, &physicalHeight);
	if (framebuffer == nullptr || physicalWidth != BottomScreenHeight ||
		physicalHeight != BottomScreenWidth)
	{
		return;
	}
	OverlayLastDrawMilliseconds = now;
	if (desired == EBottomPresentation::Progress)
	{
		BottomPresentation = EBottomPresentation::Progress;
		DrawDiagnosticProgress(framebuffer);
		I_3DSCleanDataCache(framebuffer, BottomScreenWidth * BottomScreenHeight * 4u);
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
		return;
	}
	if (desired == EBottomPresentation::NativeMenu)
	{
		// PolyFrameBuffer owns native-menu presentation after worker completion.
		BottomPresentation = EBottomPresentation::NativeMenu;
		return;
	}
	if (desired == EBottomPresentation::DeveloperOverlay)
	{
		BottomPresentation = EBottomPresentation::DeveloperOverlay;
		DrawDeveloperOverlay(framebuffer);
		I_3DSCleanDataCache(framebuffer, BottomScreenWidth * BottomScreenHeight * 4u);
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
		return;
	}

	if (gamestate != GS_LEVEL)
	{
		const EBottomPresentation target = desired;
		if (!force && BottomPresentation == target) return;
		if (target == EBottomPresentation::Menu ||
			target == EBottomPresentation::MenuDimmed)
		{
			// ZMAPINFO dims the upper menu background by 72%. Match it on the
			// lower LCD so opening Start/New Game feels like one two-screen UI.
			DrawMenuBottomScreen(framebuffer,
				target == EBottomPresentation::MenuDimmed ? 71u : 255u);
		}
		else
		{
			OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight,
				OverlayParchment);
		}
		BottomPresentation = target;
		I_3DSCleanDataCache(framebuffer, BottomScreenWidth * BottomScreenHeight * 4u);
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
		return;
	}

	BottomPresentation = EBottomPresentation::Gameplay;
	if (OverlayNotification[0] != '\0' && now >= OverlayNotificationUntilMilliseconds)
	{
		OverlayNotification[0] = '\0';
	}
	DrawBottomGameplay(framebuffer);

	I_3DSCleanDataCache(framebuffer, BottomScreenWidth * BottomScreenHeight * 4u);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

unsigned LoadingProgressForStage(const char *stage)
{
	if (stage == nullptr) return LoadingScreenProgress;
	struct FStageProgress { const char *Name; unsigned Progress; };
	static constexpr FStageProgress Stages[] = {
		{ "loading-screen-ready", 3 }, { "sdl-core-ready", 5 },
		{ "runtime-data-sd-ready", 8 }, { "runtime-data-romfs-ready", 8 },
		{ "command-line-ready", 10 }, { "joystick-ready", 12 },
		{ "game-main-enter", 14 }, { "after D_DoomInit", 16 },
		{ "after IWAD manager", 18 }, { "after PClass/PType init", 20 },
		{ "after IWAD selection", 22 }, { "after command-line files", 24 },
		{ "after filesystem init", 28 }, { "cpu-detection-enter", 30 },
		{ "cpu-detection-ready", 31 }, { "palette-enter", 32 },
		{ "palette-ready", 38 }, { "screen-size-ready", 39 },
		{ "video-init-ready", 40 }, { "sound-system-ready", 42 },
		{ "startup-screen-ready", 44 }, { "after sound definitions", 46 },
		{ "after map definitions", 48 }, { "after texture manager", 70 },
		{ "after texture patches", 71 }, { "after texture animations", 72 },
		{ "after console background", 73 }, { "after V_InitFonts", 76 },
		{ "after Doom fonts", 77 }, { "after translations", 80 },
		{ "after generic UI", 81 }, { "after team definitions", 82 },
		{ "after translate definitions", 83 }, { "actors: compiler environment", 84 },
		{ "actors: ZScript parsed", 92 }, { "actors: DECORATE parsed", 93 },
		{ "actors: functions built", 96 }, { "sdl-video-enter", 97 },
		{ "sdl-video-ready", 98 }, { "video-object-ready", 98 },
		{ "sdl-window-ready", 99 }, { "novagl-enter", 99 },
	};
	for (const auto &entry : Stages)
	{
		if (std::strcmp(stage, entry.Name) == 0) return entry.Progress;
	}
	return LoadingScreenProgress;
}

const char *LoadingStatus(unsigned progress)
{
	if (progress < 14) return "STARTING ENGINE";
	if (progress < 44) return "LOADING GAME DATA";
	if (progress < 84) return "BUILDING TEXTURES";
	if (progress < 97) return "COMPILING ACTORS";
	return "STARTING VIDEO";
}

void DrawLoadingScreen(unsigned progress)
{
	if (!LoadingScreenActive || LoadingScreenFinished) return;
	if (progress > 100) progress = 100;
	LoadingScreenProgress = std::max(LoadingScreenProgress, progress);

	u16 physicalWidth = 0;
	u16 physicalHeight = 0;
	unsigned char *framebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT,
		&physicalWidth, &physicalHeight);
	if (framebuffer == nullptr || physicalWidth != 240 || physicalHeight != 400) return;

	LoadingRect(framebuffer, 0, 0, 400, 240, 5, 8, 18);
	LoadingRect(framebuffer, 0, 0, 400, 5, 217, 74, 25);
	LoadingRect(framebuffer, 0, 235, 400, 5, 46, 119, 201);

	const char *title = "LEGEND OF DOOM";
	LoadingText(framebuffer, (400 - LoadingTextWidth(title, 4)) / 2, 35,
		title, 4, 242, 236, 213);
	LoadingRect(framebuffer, 43, 74, 314, 2, 217, 74, 25);

	const char *status = LoadingStatus(LoadingScreenProgress);
	LoadingText(framebuffer, (400 - LoadingTextWidth(status, 2)) / 2, 105,
		status, 2, 132, 183, 232);

	constexpr int BarX = 39;
	constexpr int BarY = 145;
	constexpr int BarWidth = 322;
	LoadingRect(framebuffer, BarX, BarY, BarWidth, 25, 225, 230, 235);
	LoadingRect(framebuffer, BarX + 3, BarY + 3, BarWidth - 6, 19, 13, 22, 39);
	const int fillWidth = static_cast<int>((BarWidth - 10) * LoadingScreenProgress / 100u);
	if (fillWidth > 0)
	{
		LoadingRect(framebuffer, BarX + 5, BarY + 5, fillWidth, 15, 217, 74, 25);
		LoadingRect(framebuffer, BarX + 5, BarY + 5, fillWidth, 4, 245, 151, 49);
	}
	for (int marker = 1; marker < 10; ++marker)
	{
		LoadingRect(framebuffer, BarX + 5 + marker * (BarWidth - 10) / 10,
			BarY + 5, 1, 15, 8, 12, 22);
	}

	char percent[8] = {};
	std::snprintf(percent, sizeof(percent), "%u%%", LoadingScreenProgress);
	LoadingText(framebuffer, (400 - LoadingTextWidth(percent, 3)) / 2, 188,
		percent, 3, 242, 236, 213);

	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

struct FMemorySurvey
{
	uint64_t DumpableBytes = 0;
	unsigned Regions = 0;
	unsigned DumpableRegions = 0;
	bool Complete = true;
};

struct FMemoryDumpResult
{
	uint64_t ExpectedBytes = 0;
	uint64_t WrittenBytes = 0;
	unsigned ExpectedRegions = 0;
	unsigned WrittenRegions = 0;
	unsigned Files = 0;
	bool Complete = false;
};

bool FormatPath(char *output, size_t outputSize, const char *format, ...)
{
	if (output == nullptr || outputSize == 0) return false;
	va_list args;
	va_start(args, format);
	const int count = std::vsnprintf(output, outputSize, format, args);
	va_end(args);
	if (count < 0 || static_cast<size_t>(count) >= outputSize)
	{
		output[0] = '\0';
		return false;
	}
	return true;
}

bool EnsureDirectory(const char *path)
{
	if (mkdir(path, 0777) == 0) return true;
	if (errno != EEXIST) return false;
	struct stat info = {};
	return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool PathExists(const char *path)
{
	struct stat info = {};
	return stat(path, &info) == 0;
}

void MakeTimestamp(char *output, size_t outputSize)
{
	const time_t now = time(nullptr);
	const struct tm *local = now > 0 ? localtime(&now) : nullptr;
	if (local != nullptr && std::strftime(output, outputSize, "%Y%m%d-%H%M%S", local) != 0)
	{
		return;
	}
	std::snprintf(output, outputSize, "tick-%llu",
		static_cast<unsigned long long>(svcGetSystemTick()));
}

const char *DumpModeName(EDiagnosticDumpMode mode)
{
	return mode == EDiagnosticDumpMode::Full ? "full" : "quick";
}

unsigned HighestExistingDumpSerial()
{
	unsigned highest = 0;
	DIR *directory = opendir(DumpDirectory);
	if (directory == nullptr) return highest;
	while (dirent *entry = readdir(directory))
	{
		const char *name = entry->d_name;
		if (std::strlen(name) >= 4 &&
			std::isdigit(static_cast<unsigned char>(name[0])) &&
			std::isdigit(static_cast<unsigned char>(name[1])) &&
			std::isdigit(static_cast<unsigned char>(name[2])) && name[3] == '-')
		{
			const unsigned value = static_cast<unsigned>(name[0] - '0') * 100u +
				static_cast<unsigned>(name[1] - '0') * 10u +
				static_cast<unsigned>(name[2] - '0');
			highest = std::max(highest, value);
		}
	}
	closedir(directory);
	return highest;
}

bool CreateSessionDirectory(char *output, size_t outputSize, EDiagnosticDumpMode mode)
{
	if (!EnsureDirectory(AppDirectory) || !EnsureDirectory(DumpDirectory)) return false;

	char timestamp[48] = {};
	MakeTimestamp(timestamp, sizeof(timestamp));
	const unsigned existing = HighestExistingDumpSerial();
	unsigned cached = DumpSerial.load(std::memory_order_relaxed);
	while (cached < existing && !DumpSerial.compare_exchange_weak(cached,
		existing, std::memory_order_relaxed, std::memory_order_relaxed))
	{
	}
	for (unsigned attempt = 0; attempt < 1000; ++attempt)
	{
		// Put the human sequence first and start at 001. The directory scan above
		// carries that sequence across launches without a separate state file.
		const unsigned serial = DumpSerial.fetch_add(1,
			std::memory_order_relaxed) + 1;
		if (!FormatPath(output, outputSize, "%s/%03u-%s-%s", DumpDirectory,
			serial, DumpModeName(mode), timestamp))
		{
			return false;
		}
		if (mkdir(output, 0777) == 0) return true;
		if (errno != EEXIST) break;
	}
	output[0] = '\0';
	return false;
}

bool QueryFreeSpace(uint64_t &bytes)
{
	struct statvfs info = {};
	if (statvfs(AppDirectory, &info) != 0) return false;
	const uint64_t blockSize = info.f_frsize != 0 ? info.f_frsize : info.f_bsize;
	bytes = static_cast<uint64_t>(info.f_bavail) * blockSize;
	return true;
}

bool HasSpaceFor(uint64_t bytes)
{
	uint64_t freeBytes = 0;
	if (!QueryFreeSpace(freeBytes)) return true;
	return freeBytes >= bytes + FreeSpaceReserveBytes;
}

uint64_t BoundedFileSize(const char *path, uint64_t maximum)
{
	struct stat info = {};
	if (path == nullptr || stat(path, &info) != 0 || info.st_size <= 0) return 0;
	return std::min<uint64_t>(static_cast<uint64_t>(info.st_size), maximum);
}

uint64_t DiagnosticArtifactBudget()
{
	return DumpMetadataBudgetBytes +
		BoundedFileSize(BootLogPath, MaxCopiedLogBytes) +
		BoundedFileSize(StartupLogPath, MaxCopiedLogBytes) +
		BoundedFileSize(FatalLogPath, MaxCopiedLogBytes) +
		BoundedFileSize(ConfigPath, MaxConfigInputBytes) +
		BoundedFileSize(TelemetryPath, MaxCopiedTelemetryBytes) +
		BoundedFileSize(NovaLitePath, MaxCopiedTelemetryBytes) +
		BoundedFileSize(NovaGLLogPath, MaxCopiedRendererLogBytes);
}

bool IsReadableRange(uintptr_t address, size_t size)
{
	if (address == 0 || size == 0 || address >= UserAddressLimit ||
		address + size < address || address + size > UserAddressLimit)
	{
		return false;
	}

	const uintptr_t end = address + size;
	while (address < end)
	{
		MemInfo info = {};
		PageInfo page = {};
		if (R_FAILED(svcQueryMemory(&info, &page, static_cast<u32>(address))) ||
			(info.perm & MEMPERM_READ) == 0 || info.size == 0)
		{
			return false;
		}
		const uint64_t regionEnd64 = static_cast<uint64_t>(info.base_addr) + info.size;
		if (regionEnd64 <= address || regionEnd64 > UserAddressLimit) return false;
		address = std::min(end, static_cast<uintptr_t>(regionEnd64));
	}
	return true;
}

uint64_t UpdateFnv1a(uint64_t hash, const unsigned char *data, size_t size)
{
	for (size_t index = 0; index < size; ++index)
	{
		hash ^= data[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

bool WriteRangeAtomic(const char *path, uintptr_t address, size_t size,
	uint64_t &hash, size_t &writtenBytes)
{
	writtenBytes = 0;
	hash = UINT64_C(14695981039346656037);
	if (path == nullptr || !IsReadableRange(address, size) || !HasSpaceFor(size)) return false;

	char partialPath[384] = {};
	if (!FormatPath(partialPath, sizeof(partialPath), "%s.part", path)) return false;
	FILE *file = std::fopen(partialPath, "wb");
	if (file == nullptr) return false;

	bool ok = true;
	while (writtenBytes < size)
	{
		const size_t chunk = std::min(IoChunkBytes, size - writtenBytes);
		const uintptr_t source = address + writtenBytes;
		if (!IsReadableRange(source, chunk))
		{
			ok = false;
			break;
		}
		std::memmove(IoBuffer, reinterpret_cast<const void *>(source), chunk);
		hash = UpdateFnv1a(hash, IoBuffer, chunk);
		if (std::fwrite(IoBuffer, 1, chunk, file) != chunk)
		{
			ok = false;
			break;
		}
		writtenBytes += chunk;
	}

	if (std::fflush(file) != 0 || std::fclose(file) != 0) ok = false;
	if (ok && writtenBytes == size && std::rename(partialPath, path) == 0) return true;
	std::remove(partialPath);
	return false;
}

bool FinishAtomicFile(FILE *file, const char *partialPath, const char *finalPath)
{
	if (file == nullptr) return false;
	bool ok = std::fflush(file) == 0;
	if (std::fclose(file) != 0) ok = false;
	if (ok && std::rename(partialPath, finalPath) == 0) return true;
	std::remove(partialPath);
	return false;
}

void WriteU16(FILE *file, uint16_t value)
{
	std::fputc(static_cast<int>(value & 0xffu), file);
	std::fputc(static_cast<int>((value >> 8) & 0xffu), file);
}

void WriteU32(FILE *file, uint32_t value)
{
	WriteU16(file, static_cast<uint16_t>(value));
	WriteU16(file, static_cast<uint16_t>(value >> 16));
}

bool DecodeFramebufferPixel(GSPGPU_FramebufferFormat format, const unsigned char *pixel,
	unsigned char *bgr)
{
	switch (format)
	{
	case GSP_RGBA8_OES:
		// RGBA8888 is stored as A, B, G, R on little-endian ARM.
		bgr[0] = pixel[1];
		bgr[1] = pixel[2];
		bgr[2] = pixel[3];
		return true;

	case GSP_BGR8_OES:
		bgr[0] = pixel[0];
		bgr[1] = pixel[1];
		bgr[2] = pixel[2];
		return true;

	case GSP_RGB565_OES:
	{
		const uint16_t value = static_cast<uint16_t>(pixel[0] | (pixel[1] << 8));
		bgr[0] = static_cast<unsigned char>((value & 31u) * 255u / 31u);
		bgr[1] = static_cast<unsigned char>(((value >> 5) & 63u) * 255u / 63u);
		bgr[2] = static_cast<unsigned char>(((value >> 11) & 31u) * 255u / 31u);
		return true;
	}

	case GSP_RGB5_A1_OES:
	{
		const uint16_t value = static_cast<uint16_t>(pixel[0] | (pixel[1] << 8));
		bgr[0] = static_cast<unsigned char>(((value >> 1) & 31u) * 255u / 31u);
		bgr[1] = static_cast<unsigned char>(((value >> 6) & 31u) * 255u / 31u);
		bgr[2] = static_cast<unsigned char>(((value >> 11) & 31u) * 255u / 31u);
		return true;
	}

	case GSP_RGBA4_OES:
	{
		const uint16_t value = static_cast<uint16_t>(pixel[0] | (pixel[1] << 8));
		bgr[0] = static_cast<unsigned char>(((value >> 4) & 15u) * 17u);
		bgr[1] = static_cast<unsigned char>(((value >> 8) & 15u) * 17u);
		bgr[2] = static_cast<unsigned char>(((value >> 12) & 15u) * 17u);
		return true;
	}
	}
	return false;
}

bool WriteFramebufferBmp(const char *path, const unsigned char *framebuffer, uint16_t physicalWidth,
	uint16_t physicalHeight, GSPGPU_FramebufferFormat format)
{
	if (path == nullptr || framebuffer == nullptr || physicalWidth == 0 || physicalHeight == 0)
	{
		return false;
	}
	const unsigned bytesPerPixel = gspGetBytesPerPixel(format);
	if (bytesPerPixel == 0 || physicalHeight > 800) return false;

	const uint32_t outputWidth = physicalHeight;
	const uint32_t outputHeight = physicalWidth;
	const uint32_t rowBytes = outputWidth * 3u;
	const uint32_t padding = (4u - (rowBytes & 3u)) & 3u;
	const uint32_t imageBytes = (rowBytes + padding) * outputHeight;
	const uint32_t fileBytes = 54u + imageBytes;

	char partialPath[384] = {};
	if (!FormatPath(partialPath, sizeof(partialPath), "%s.part", path) ||
		!HasSpaceFor(fileBytes))
	{
		return false;
	}
	FILE *file = std::fopen(partialPath, "wb");
	if (file == nullptr) return false;

	std::fputc('B', file);
	std::fputc('M', file);
	WriteU32(file, fileBytes);
	WriteU16(file, 0);
	WriteU16(file, 0);
	WriteU32(file, 54u);
	WriteU32(file, 40u);
	WriteU32(file, outputWidth);
	WriteU32(file, outputHeight);
	WriteU16(file, 1);
	WriteU16(file, 24);
	WriteU32(file, 0);
	WriteU32(file, imageBytes);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	bool ok = !std::ferror(file);
	for (int y = static_cast<int>(outputHeight) - 1; y >= 0 && ok; --y)
	{
		for (uint32_t x = 0; x < outputWidth; ++x)
		{
			const size_t physicalIndex = static_cast<size_t>(x) * physicalWidth +
				(physicalWidth - 1u - static_cast<uint32_t>(y));
			if (!DecodeFramebufferPixel(format, framebuffer + physicalIndex * bytesPerPixel,
				IoBuffer + x * 3u))
			{
				ok = false;
				break;
			}
		}
		if (ok)
		{
			std::memset(IoBuffer + rowBytes, 0, padding);
			ok = std::fwrite(IoBuffer, 1, rowBytes + padding, file) == rowBytes + padding;
		}
	}

	if (!ok)
	{
		std::fclose(file);
		std::remove(partialPath);
		return false;
	}
	return FinishAtomicFile(file, partialPath, path);
}

bool CaptureScreenSnapshot(gfxScreen_t screen, FScreenSnapshot &snapshot)
{
	uint16_t width = 0;
	uint16_t height = 0;
	const unsigned char *framebuffer = gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);
	const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(screen);
	const unsigned bytesPerPixel = gspGetBytesPerPixel(format);
	if (framebuffer == nullptr || width == 0 || height == 0 || bytesPerPixel == 0)
	{
		return false;
	}

	const size_t size = static_cast<size_t>(width) * height * bytesPerPixel;
	unsigned char *pixels = static_cast<unsigned char *>(linearAlloc(size));
	if (pixels == nullptr) return false;
	std::memcpy(pixels, framebuffer, size);

	snapshot.Width = width;
	snapshot.Height = height;
	snapshot.Format = format;
	snapshot.BytesPerPixel = bytesPerPixel;
	snapshot.Size = size;
	snapshot.Pixels = pixels;
	return true;
}

bool WriteScreenCapture(FILE *manifest, const char *directory,
	const FScreenSnapshot &snapshot, const char *name)
{
	if (snapshot.Pixels == nullptr || snapshot.Width == 0 || snapshot.Height == 0 ||
		snapshot.BytesPerPixel == 0 || snapshot.Size == 0)
	{
		std::fprintf(manifest, "screen.%s=unavailable\n", name);
		return false;
	}

	char rawPath[384] = {};
	char bmpPath[384] = {};
	FormatPath(rawPath, sizeof(rawPath), "%s/%s-framebuffer.bin", directory, name);
	FormatPath(bmpPath, sizeof(bmpPath), "%s/%s-screen.bmp", directory, name);
	uint64_t hash = 0;
	size_t written = 0;
	const bool rawOk = WriteRangeAtomic(rawPath, reinterpret_cast<uintptr_t>(snapshot.Pixels),
		snapshot.Size, hash, written);
	const bool bmpOk = WriteFramebufferBmp(bmpPath, snapshot.Pixels, snapshot.Width,
		snapshot.Height, snapshot.Format);

	std::fprintf(manifest,
		"screen.%s=physical:%ux%u logical:%ux%u format:%u bpp:%u raw:%s "
		"raw_bytes:%lu raw_fnv1a64:%016llx bmp:%s\n",
		name, snapshot.Width, snapshot.Height, snapshot.Height, snapshot.Width,
		static_cast<unsigned>(snapshot.Format), snapshot.BytesPerPixel,
		rawOk ? "ok" : "failed", static_cast<unsigned long>(written),
		static_cast<unsigned long long>(hash), bmpOk ? "ok" : "failed");
	return rawOk && bmpOk;
}

char AsciiLower(char value)
{
	return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool ContainsInsensitive(const char *text, const char *needle)
{
	if (text == nullptr || needle == nullptr || *needle == '\0') return false;
	const size_t needleLength = std::strlen(needle);
	for (const char *start = text; *start != '\0'; ++start)
	{
		size_t index = 0;
		while (index < needleLength && start[index] != '\0' &&
			AsciiLower(start[index]) == AsciiLower(needle[index]))
		{
			++index;
		}
		if (index == needleLength) return true;
	}
	return false;
}

bool IsSensitiveLine(const char *line)
{
	static const char *const sensitiveWords[] = {
		"password", "passwd", "authorization", "api_key", "apikey", "token", "secret",
		"cookie", "credential"
	};
	for (const char *word : sensitiveWords)
	{
		if (ContainsInsensitive(line, word)) return true;
	}
	return false;
}

bool CopySanitizedBootLog(const char *destination, size_t &copiedBytes, unsigned &redactedLines)
{
	copiedBytes = 0;
	redactedLines = 0;
	FILE *source = std::fopen(BootLogPath, "rb");
	if (source == nullptr) return false;

	char partialPath[384] = {};
	if (!FormatPath(partialPath, sizeof(partialPath), "%s.part", destination))
	{
		std::fclose(source);
		return false;
	}
	FILE *output = std::fopen(partialPath, "wb");
	if (output == nullptr)
	{
		std::fclose(source);
		return false;
	}

	char line[2048] = {};
	bool ok = true;
	while (copiedBytes < MaxCopiedLogBytes && std::fgets(line, sizeof(line), source) != nullptr)
	{
		const char *writeLine = line;
		if (IsSensitiveLine(line))
		{
			writeLine = "[redacted potentially sensitive log line]\n";
			++redactedLines;
		}
		const size_t lineBytes = std::strlen(writeLine);
		if (copiedBytes + lineBytes > MaxCopiedLogBytes) break;
		if (std::fwrite(writeLine, 1, lineBytes, output) != lineBytes)
		{
			ok = false;
			break;
		}
		copiedBytes += lineBytes;
	}
	if (!std::feof(source))
	{
		static const char truncated[] = "\n[boot log copy truncated at 4 MiB]\n";
		ok = ok && std::fwrite(truncated, 1, sizeof(truncated) - 1, output) == sizeof(truncated) - 1;
	}
	std::fclose(source);
	if (!ok)
	{
		std::fclose(output);
		std::remove(partialPath);
		return false;
	}
	return FinishAtomicFile(output, partialPath, destination);
}

bool CopyDiagnosticFile(const char *sourcePath, const char *destination, size_t maxBytes,
	size_t &copiedBytes, uint64_t &hash, bool &truncated)
{
	copiedBytes = 0;
	hash = UINT64_C(14695981039346656037);
	truncated = false;
	FILE *source = std::fopen(sourcePath, "rb");
	if (source == nullptr) return false;

	char partialPath[384] = {};
	if (!FormatPath(partialPath, sizeof(partialPath), "%s.part", destination))
	{
		std::fclose(source);
		return false;
	}
	FILE *output = std::fopen(partialPath, "wb");
	if (output == nullptr)
	{
		std::fclose(source);
		return false;
	}

	bool ok = true;
	while (copiedBytes < maxBytes)
	{
		const size_t request = std::min(IoChunkBytes, maxBytes - copiedBytes);
		const size_t count = std::fread(IoBuffer, 1, request, source);
		if (count != 0)
		{
			if (!HasSpaceFor(count) || std::fwrite(IoBuffer, 1, count, output) != count)
			{
				ok = false;
				break;
			}
			hash = UpdateFnv1a(hash, IoBuffer, count);
			copiedBytes += count;
		}
		if (count < request)
		{
			if (std::ferror(source)) ok = false;
			break;
		}
	}
	if (ok && copiedBytes == maxBytes)
	{
		truncated = std::fgetc(source) != EOF;
	}
	std::fclose(source);
	if (!ok)
	{
		std::fclose(output);
		std::remove(partialPath);
		return false;
	}
	return FinishAtomicFile(output, partialPath, destination);
}

bool CopyDiagnosticArtifact(FILE *manifest, const char *directory, const char *key,
	const char *sourcePath, const char *outputName, size_t maxBytes)
{
	const bool exists = PathExists(sourcePath);
	char destination[384] = {};
	if (!FormatPath(destination, sizeof(destination), "%s/%s", directory, outputName))
	{
		std::fprintf(manifest, "%s=failed_path\n", key);
		return false;
	}
	size_t bytes = 0;
	uint64_t hash = 0;
	bool truncated = false;
	const bool ok = exists && CopyDiagnosticFile(sourcePath, destination, maxBytes, bytes, hash,
		truncated);
	std::fprintf(manifest, "%s=%s bytes=%lu fnv1a64=%016llx truncated=%s source=%s\n",
		key, ok ? "ok" : (exists ? "failed" : "unavailable"),
		static_cast<unsigned long>(bytes), static_cast<unsigned long long>(hash),
		truncated ? "yes" : "no", sourcePath);
	return !exists || ok;
}

bool StartsWithInsensitive(const char *text, const char *prefix)
{
	while (*prefix != '\0')
	{
		if (*text == '\0' || AsciiLower(*text) != AsciiLower(*prefix)) return false;
		++text;
		++prefix;
	}
	return true;
}

bool IsAllowedConfigKey(const char *key)
{
	static const char *const prefixes[] = {
		"vid_", "gl_", "r_", "snd_", "joy_", "mouse_", "m_"
	};
	static const char *const exact[] = {
		"use_joystick", "cl_capfps", "screenblocks", "fullscreen", "win_w", "win_h"
	};
	for (const char *prefix : prefixes)
	{
		if (StartsWithInsensitive(key, prefix)) return true;
	}
	for (const char *name : exact)
	{
		if (StartsWithInsensitive(key, name) && key[std::strlen(name)] == '\0') return true;
	}
	return false;
}

char *Trim(char *text)
{
	while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) ++text;
	char *end = text + std::strlen(text);
	while (end > text && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
	*end = '\0';
	return text;
}

bool WriteSanitizedConfig(const char *destination, unsigned &writtenKeys, unsigned &skippedLines)
{
	writtenKeys = 0;
	skippedLines = 0;
	FILE *source = std::fopen(ConfigPath, "rb");
	if (source == nullptr) return false;

	char partialPath[384] = {};
	if (!FormatPath(partialPath, sizeof(partialPath), "%s.part", destination))
	{
		std::fclose(source);
		return false;
	}
	FILE *output = std::fopen(partialPath, "wb");
	if (output == nullptr)
	{
		std::fclose(source);
		return false;
	}

	std::fprintf(output,
		"# Sanitized diagnostic subset. Paths, resource lists, aliases, saves, and credentials are omitted.\n");
	char line[2048] = {};
	size_t inputBytes = 0;
	bool ok = true;
	while (inputBytes < MaxConfigInputBytes && std::fgets(line, sizeof(line), source) != nullptr)
	{
		inputBytes += std::strlen(line);
		char *equals = std::strchr(line, '=');
		if (equals == nullptr)
		{
			++skippedLines;
			continue;
		}
		*equals = '\0';
		char *key = Trim(line);
		char *value = Trim(equals + 1);
		if (!IsAllowedConfigKey(key) || IsSensitiveLine(key) || IsSensitiveLine(value))
		{
			++skippedLines;
			continue;
		}
		if (std::fprintf(output, "%s=%s\n", key, value) < 0)
		{
			ok = false;
			break;
		}
		++writtenKeys;
	}
	std::fclose(source);
	if (!ok)
	{
		std::fclose(output);
		std::remove(partialPath);
		return false;
	}
	return FinishAtomicFile(output, partialPath, destination);
}

const char *MemoryStateName(uint32_t state)
{
	switch (state)
	{
	case MEMSTATE_FREE: return "free";
	case MEMSTATE_RESERVED: return "reserved";
	case MEMSTATE_IO: return "io";
	case MEMSTATE_STATIC: return "static";
	case MEMSTATE_CODE: return "code";
	case MEMSTATE_PRIVATE: return "private";
	case MEMSTATE_SHARED: return "shared";
	case MEMSTATE_CONTINUOUS: return "continuous";
	case MEMSTATE_ALIASED: return "aliased";
	case MEMSTATE_ALIAS: return "alias";
	case MEMSTATE_ALIASCODE: return "aliascode";
	case MEMSTATE_LOCKED: return "locked";
	default: return "unknown";
	}
}

bool IsDumpableMemory(const MemInfo &info)
{
	if ((info.perm & MEMPERM_READ) == 0) return false;
	// Include every readable mapping exposed in this process' user address
	// space, including code, heap, stacks, linear, aliases and shared memory.
	// Free/reserved ranges have no readable payload; direct I/O is deliberately
	// never dereferenced because reads may have device side effects.
	return info.state != MEMSTATE_FREE && info.state != MEMSTATE_RESERVED &&
		info.state != MEMSTATE_IO;
}

bool NextMemoryRegion(uintptr_t &address, MemInfo &info, PageInfo &page)
{
	if (address >= UserAddressLimit) return false;
	if (R_FAILED(svcQueryMemory(&info, &page, static_cast<u32>(address))) || info.size == 0)
	{
		return false;
	}
	const uint64_t end = static_cast<uint64_t>(info.base_addr) + info.size;
	if (end <= address || end > UserAddressLimit) return false;
	address = static_cast<uintptr_t>(end);
	return true;
}

FMemorySurvey SurveyMemory(FILE *map)
{
	FMemorySurvey result;
	uintptr_t address = 0;
	if (map != nullptr)
	{
		std::fprintf(map,
			"# svcQueryMemory map. Dump policy: every readable user mapping except free, reserved and I/O.\n");
	}
	for (unsigned index = 0; address < UserAddressLimit && index < MaxMemoryRegions; ++index)
	{
		const uintptr_t query = address;
		MemInfo info = {};
		PageInfo page = {};
		if (!NextMemoryRegion(address, info, page))
		{
			if (map != nullptr)
			{
				std::fprintf(map, "query_error address=%08lx result=stopped\n",
					static_cast<unsigned long>(query));
			}
			result.Complete = false;
			break;
		}
		const bool dumpable = IsDumpableMemory(info);
		if (map != nullptr)
		{
			std::fprintf(map,
				"region=%04u base=%08lx end=%08lx size=%lu state=%lu(%s) perm=%c%c%c "
				"page_flags=%08lx dump=%s\n",
				index, static_cast<unsigned long>(info.base_addr),
				static_cast<unsigned long>(info.base_addr + info.size),
				static_cast<unsigned long>(info.size), static_cast<unsigned long>(info.state),
				MemoryStateName(info.state), (info.perm & MEMPERM_READ) ? 'r' : '-',
				(info.perm & MEMPERM_WRITE) ? 'w' : '-',
				(info.perm & MEMPERM_EXECUTE) ? 'x' : '-',
				static_cast<unsigned long>(page.flags), dumpable ? "yes" : "no");
		}
		++result.Regions;
		if (dumpable)
		{
			result.DumpableBytes += info.size;
			++result.DumpableRegions;
		}
	}
	if (address < UserAddressLimit && result.Regions >= MaxMemoryRegions) result.Complete = false;
	if (map != nullptr)
	{
		std::fprintf(map,
			"survey regions=%u dumpable_regions=%u dumpable_bytes=%llu complete=%s\n\n",
			result.Regions, result.DumpableRegions,
			static_cast<unsigned long long>(result.DumpableBytes),
			result.Complete ? "yes" : "no");
	}
	return result;
}

FMemoryDumpResult WriteMemorySurvey(FILE *manifest, const char *directory)
{
	FMemoryDumpResult result;
	char mapPath[384] = {};
	char mapPartialPath[384] = {};
	if (!FormatPath(mapPath, sizeof(mapPath), "%s/memory-map.txt", directory) ||
		!FormatPath(mapPartialPath, sizeof(mapPartialPath), "%s.part", mapPath))
	{
		std::fprintf(manifest, "memory.status=failed_to_create_map_path\n");
		return result;
	}

	FILE *map = std::fopen(mapPartialPath, "wb");
	if (map == nullptr)
	{
		std::fprintf(manifest, "memory.status=failed_to_open_map\n");
		return result;
	}
	const FMemorySurvey survey = SurveyMemory(map);
	result.ExpectedBytes = survey.DumpableBytes;
	result.ExpectedRegions = survey.DumpableRegions;
	std::fprintf(map,
		"payload=omitted mode=quick reason=memory_image_is_reserved_for_full_dump\n");
	const bool mapOk = FinishAtomicFile(map, mapPartialPath, mapPath);
	result.Complete = survey.Complete && mapOk;
	std::fprintf(manifest,
		"memory.status=omitted_quick expected_bytes=%llu expected_regions=%u map=%s\n",
		static_cast<unsigned long long>(result.ExpectedBytes), result.ExpectedRegions,
		mapOk ? "ok" : "failed");
	return result;
}

FMemoryDumpResult DumpReadableMemory(FILE *manifest, const char *directory)
{
	FMemoryDumpResult result;
	char memoryPath[384] = {};
	char memoryPartialPath[384] = {};
	char mapPath[384] = {};
	char mapPartialPath[384] = {};
	if (!FormatPath(memoryPath, sizeof(memoryPath), "%s/memory.bin", directory) ||
		!FormatPath(memoryPartialPath, sizeof(memoryPartialPath), "%s.part", memoryPath) ||
		!FormatPath(mapPath, sizeof(mapPath), "%s/memory-map.txt", directory) ||
		!FormatPath(mapPartialPath, sizeof(mapPartialPath), "%s.part", mapPath))
	{
		std::fprintf(manifest, "memory.status=failed_to_create_output\n");
		return result;
	}

	FILE *map = std::fopen(mapPartialPath, "wb");
	if (map == nullptr)
	{
		std::fprintf(manifest, "memory.status=failed_to_open_map\n");
		return result;
	}
	const FMemorySurvey survey = SurveyMemory(map);
	result.ExpectedBytes = survey.DumpableBytes;
	result.ExpectedRegions = survey.DumpableRegions;
	DiagnosticProgressMaximum = result.ExpectedBytes;
	DiagnosticProgressValue = 0;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage),
		"WRITING MEMORY");
	DrawBottomOverlay(true);

	uint64_t freeBefore = 0;
	const bool freeKnown = QueryFreeSpace(freeBefore);
	std::fprintf(map, "# Dump results\n");
	std::fprintf(map, "space_before=%s%llu reserve=%llu\n", freeKnown ? "" : "unknown/",
		static_cast<unsigned long long>(freeBefore),
		static_cast<unsigned long long>(FreeSpaceReserveBytes));
	std::fprintf(map,
		"payload_file=memory.bin layout=concatenated_readable_regions "
		"hash=omitted_on_device reason=full_dump_speed\n");

	FILE *memory = std::fopen(memoryPartialPath, "wb");
	if (memory == nullptr)
	{
		std::fprintf(map, "payload_status=failed_to_open errno=%d\n", errno);
	}
	bool allOk = survey.Complete && memory != nullptr;
	bool stop = false;
	const uint64_t startedMilliseconds = osGetTime();
	uint64_t nextProgressBytes = DumpProgressStepBytes;
	uintptr_t address = 0;
	unsigned dumpableIndex = 0;
	for (unsigned regionIndex = 0;
		memory != nullptr && address < UserAddressLimit &&
		regionIndex < MaxMemoryRegions && !stop; ++regionIndex)
	{
		MemInfo info = {};
		PageInfo page = {};
		if (!NextMemoryRegion(address, info, page))
		{
			allOk = false;
			break;
		}
		if (!IsDumpableMemory(info)) continue;

		bool regionOk = true;
		size_t regionWritten = 0;
		const uint64_t payloadOffset = result.WrittenBytes;
		int regionError = 0;
		while (regionWritten < info.size)
		{
			const size_t chunk = std::min(IoChunkBytes,
				static_cast<size_t>(info.size) - regionWritten);
			const uintptr_t source = static_cast<uintptr_t>(info.base_addr) + regionWritten;
			if (!IsReadableRange(source, chunk))
			{
				regionOk = false;
				stop = true;
				break;
			}
			std::memmove(IoBuffer, reinterpret_cast<const void *>(source), chunk);
			errno = 0;
			const size_t written = std::fwrite(IoBuffer, 1, chunk, memory);
			if (written != chunk)
			{
				regionWritten += written;
				result.WrittenBytes += written;
				regionError = errno;
				regionOk = false;
				stop = true;
				break;
			}
			regionWritten += written;
			result.WrittenBytes += written;
			DiagnosticProgressValue = result.WrittenBytes;
			if (result.WrittenBytes >= nextProgressBytes ||
				result.WrittenBytes == result.ExpectedBytes)
			{
				nextProgressBytes = result.WrittenBytes + DumpProgressStepBytes;
				DrawBottomOverlay(true);
			}
		}
		std::fprintf(map,
			"region_result=%03u source=%08lx payload_offset=%llu written=%lu "
			"expected=%lu status=%s errno=%d\n",
			dumpableIndex, static_cast<unsigned long>(info.base_addr),
			static_cast<unsigned long long>(payloadOffset),
			static_cast<unsigned long>(regionWritten), static_cast<unsigned long>(info.size),
			regionOk ? "ok" : "failed", regionError);
		if (regionOk) ++result.WrittenRegions;
		else allOk = false;
		++dumpableIndex;
	}
	if (stop) allOk = false;

	bool payloadOk = memory != nullptr;
	if (memory != nullptr)
	{
		if (std::fflush(memory) != 0) payloadOk = false;
		if (std::fclose(memory) != 0) payloadOk = false;
		if (payloadOk && allOk && result.WrittenBytes == result.ExpectedBytes &&
			result.WrittenRegions == result.ExpectedRegions &&
			std::rename(memoryPartialPath, memoryPath) == 0)
		{
			result.Files = 1;
		}
		else
		{
			payloadOk = false;
			std::remove(memoryPartialPath);
		}
	}
	result.Complete = payloadOk && allOk && result.WrittenBytes == result.ExpectedBytes &&
		result.WrittenRegions == result.ExpectedRegions && result.Files == 1;
	const uint64_t elapsedMilliseconds = osGetTime() - startedMilliseconds;
	const uint64_t throughputKiBPerSecond = elapsedMilliseconds == 0 ? 0 :
		(result.WrittenBytes * 1000u) / (elapsedMilliseconds * 1024u);

	uint64_t freeAfter = 0;
	const bool freeAfterKnown = QueryFreeSpace(freeAfter);
	std::fprintf(map,
		"summary expected_bytes=%llu written_bytes=%llu expected_regions=%u "
		"written_regions=%u files=%u elapsed_ms=%llu throughput_kib_s=%llu "
		"space_after=%s%llu complete=%s\n",
		static_cast<unsigned long long>(result.ExpectedBytes),
		static_cast<unsigned long long>(result.WrittenBytes), result.ExpectedRegions,
		result.WrittenRegions, result.Files,
		static_cast<unsigned long long>(elapsedMilliseconds),
		static_cast<unsigned long long>(throughputKiBPerSecond),
		freeAfterKnown ? "" : "unknown/",
		static_cast<unsigned long long>(freeAfter), result.Complete ? "yes" : "no");
	const bool mapOk = FinishAtomicFile(map, mapPartialPath, mapPath);
	if (!mapOk) result.Complete = false;

	std::fprintf(manifest,
		"memory.status=%s expected_bytes=%llu written_bytes=%llu expected_regions=%u "
		"written_regions=%u files=%u payload=memory.bin hash=omitted_on_device "
		"elapsed_ms=%llu throughput_kib_s=%llu map=%s\n",
		result.Complete ? "complete" : "incomplete",
		static_cast<unsigned long long>(result.ExpectedBytes),
		static_cast<unsigned long long>(result.WrittenBytes), result.ExpectedRegions,
		result.WrittenRegions, result.Files,
		static_cast<unsigned long long>(elapsedMilliseconds),
		static_cast<unsigned long long>(throughputKiBPerSecond), mapOk ? "ok" : "failed");
	return result;
}

bool WriteMarker(const char *directory, const char *name, const char *text)
{
	char path[384] = {};
	if (!FormatPath(path, sizeof(path), "%s/%s", directory, name)) return false;
	FILE *file = std::fopen(path, "wb");
	if (file == nullptr) return false;
	const size_t size = std::strlen(text);
	const bool ok = std::fwrite(text, 1, size, file) == size && std::fflush(file) == 0;
	return std::fclose(file) == 0 && ok;
}

bool IsDotDirectory(const char *name)
{
	return name != nullptr && (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0);
}

bool CountDumpTreeEntries(const char *directory, unsigned depth, uint64_t &entries)
{
	if (depth > 16) return false;
	DIR *dir = opendir(directory);
	if (dir == nullptr) return false;
	bool ok = true;
	while (dirent *entry = readdir(dir))
	{
		if (IsDotDirectory(entry->d_name)) continue;
		char path[512] = {};
		if (!FormatPath(path, sizeof(path), "%s/%s", directory, entry->d_name))
		{
			ok = false;
			continue;
		}
		struct stat info = {};
		if (stat(path, &info) != 0)
		{
			ok = false;
			continue;
		}
		++entries;
		if (S_ISDIR(info.st_mode) && !CountDumpTreeEntries(path, depth + 1, entries)) ok = false;
	}
	if (closedir(dir) != 0) ok = false;
	return ok;
}

bool RemoveDumpTreeContents(const char *directory, unsigned depth, uint64_t &removed)
{
	if (depth > 16) return false;
	// Reopen after each removal. FatFs may compact directory entries when a
	// file is unlinked, so continuing the same DIR cursor can skip the entry
	// that moved into its slot.
	for (;;)
	{
		char path[512] = {};
		DIR *dir = opendir(directory);
		if (dir == nullptr) return false;
		bool hasEntry = false;
		bool pathOk = false;
		while (dirent *entry = readdir(dir))
		{
			if (IsDotDirectory(entry->d_name)) continue;
			hasEntry = true;
			pathOk = FormatPath(path, sizeof(path), "%s/%s", directory, entry->d_name);
			break;
		}
		const bool closeOk = closedir(dir) == 0;
		if (!hasEntry) return closeOk;
		if (!closeOk || !pathOk) return false;

		struct stat info = {};
		if (stat(path, &info) != 0) return false;
		bool removedOk = true;
		if (S_ISDIR(info.st_mode))
		{
			removedOk = RemoveDumpTreeContents(path, depth + 1, removed) && rmdir(path) == 0;
		}
		else
		{
			removedOk = std::remove(path) == 0;
		}
		if (!removedOk) return false;
		++removed;
		DiagnosticProgressValue = std::min(removed, DiagnosticProgressMaximum);
		DrawBottomOverlay(true);
	}
}

void CleanAllDiagnosticDumps()
{
	DiagnosticProgressActive = true;
	DiagnosticProgressMode = EDiagnosticDumpMode::Clean;
	DiagnosticProgressValue = 0;
	DiagnosticProgressMaximum = 0;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage), "SCANNING DUMPS");
	SetOverlayNotification("CLEANING DUMPS", 60000);
	DrawBottomOverlay(true);

	if (!EnsureDirectory(AppDirectory) || !EnsureDirectory(DumpDirectory))
	{
		DiagnosticProgressActive = false;
		SetOverlayNotification("CLEAN FAILED");
		DrawBottomOverlay(true);
		return;
	}

	uint64_t entries = 0;
	const bool countOk = CountDumpTreeEntries(DumpDirectory, 0, entries);
	DiagnosticProgressMaximum = std::max<uint64_t>(entries, 1);
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage), "DELETING DUMPS");
	DrawBottomOverlay(true);

	uint64_t removed = 0;
	const bool removeOk = RemoveDumpTreeContents(DumpDirectory, 0, removed);
	DiagnosticProgressValue = DiagnosticProgressMaximum;
	DrawBottomOverlay(true);
	DiagnosticProgressActive = false;
	SetOverlayNotification(countOk && removeOk ?
		(entries == 0 ? "NO DUMPS TO CLEAN" : "ALL DUMPS CLEANED") : "CLEAN INCOMPLETE");
	DrawBottomOverlay(true);
}

void WriteDiagnosticDump(EDiagnosticDumpMode mode)
{
	const bool full = mode == EDiagnosticDumpMode::Full;
	DiagnosticProgressMode = mode;
	DiagnosticProgressValue = 0;
	DiagnosticProgressMaximum = full ? 0 : 100;
	FScopedDiagnosticAudioPause audioPause;

	// Freeze both LCDs in RAM before replacing the ordinary status panel. A
	// framebuffer copy is sub-millisecond work and uses about 691 KiB total;
	// all slow FAT writes and BMP conversion happen after progress is visible.
	FScreenSnapshot topSnapshot;
	FScreenSnapshot bottomSnapshot;
	CaptureScreenSnapshot(GFX_TOP, topSnapshot);
	CaptureScreenSnapshot(GFX_BOTTOM, bottomSnapshot);
	DiagnosticProgressActive = true;
	SetOverlayNotification(full ? "SAVING FULL DUMP" : "SAVING QUICK DUMP", 60000);
	if (!full) DiagnosticProgressValue = 5;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage),
		"SAVING SCREENSHOTS");
	DrawBottomOverlay(true);
	// DrawBottomOverlay swaps the bottom buffer. Wait once so the LCD has
	// scanned the progress UI before this thread begins blocking on the SD.
	gspWaitForVBlank();

	const FMemorySurvey initialSurvey = full ? SurveyMemory(nullptr) : FMemorySurvey{};
	const uint64_t requiredBytes = DiagnosticArtifactBudget() +
		(full ? initialSurvey.DumpableBytes : 0);
	if ((full && !initialSurvey.Complete) || !HasSpaceFor(requiredBytes))
	{
		DiagnosticProgressActive = false;
		SetOverlayNotification((full && !initialSurvey.Complete) ?
			"MEMORY CHECK FAILED" : "YOUR MEMORY IS FULL");
		DrawBottomOverlay(true);
		return;
	}
	if (full) DiagnosticProgressMaximum = initialSurvey.DumpableBytes;
	char directory[320] = {};
	if (!CreateSessionDirectory(directory, sizeof(directory), mode))
	{
		std::fprintf(stderr, "[lod3ds] %s dump failed: could not create %s\n",
			DumpModeName(mode), DumpDirectory);
		DiagnosticProgressActive = false;
		SetOverlayNotification("DUMP FAILED");
		DrawBottomOverlay(true);
		std::fflush(stderr);
		return;
	}

	std::fflush(nullptr);
	char manifestPath[384] = {};
	char manifestPartialPath[384] = {};
	FormatPath(manifestPath, sizeof(manifestPath), "%s/manifest.txt", directory);
	FormatPath(manifestPartialPath, sizeof(manifestPartialPath), "%s/manifest.partial", directory);
	FILE *manifest = std::fopen(manifestPartialPath, "wb");
	if (manifest == nullptr)
	{
		std::fprintf(stderr, "[lod3ds] %s dump failed: cannot create manifest in %s\n",
			DumpModeName(mode), directory);
		DiagnosticProgressActive = false;
		SetOverlayNotification("DUMP FAILED");
		DrawBottomOverlay(true);
		std::fflush(stderr);
		return;
	}

	bool isNew3DS = false;
	const Result modelResult = APT_CheckNew3DS(&isNew3DS);
	s32 threadPriority = -1;
	svcGetThreadPriority(&threadPriority, CUR_THREAD_HANDLE);
	u32 threadId = 0;
	svcGetThreadId(&threadId, CUR_THREAD_HANDLE);
	const struct mallinfo heap = mallinfo();
	uint64_t freeBefore = 0;
	const bool freeBeforeKnown = QueryFreeSpace(freeBefore);

	std::fprintf(manifest, "Legend of Doom 3DS diagnostic dump\n");
	std::fprintf(manifest, "format_version=2\n");
	std::fprintf(manifest, "directory_format=NNN-mode-YYYYMMDD-HHMMSS\n");
	std::fprintf(manifest, "mode=%s\n", DumpModeName(mode));
	std::fprintf(manifest, "trigger=%s edge-triggered\n", full ? "L+R+X" : "L+R+A");
	std::fprintf(manifest,
		"execution=main thread, one-second arm delay, LCDs copied to RAM before progress UI, "
		"screenshots persisted after progress UI\n");
	std::fprintf(manifest, "port_version=%s\n", LOD3DS_PORT_VERSION);
	std::fprintf(manifest, "engine_version=%s\n", GetVersionString());
	std::fprintf(manifest, "engine_git_hash=%s\n", GetGitHash());
	std::fprintf(manifest, "engine_git_time=%s\n", GetGitTime());
	std::fprintf(manifest, "compiler=%s\n", __VERSION__);
#ifdef LOD3DS_BUILD_ID
	std::fprintf(manifest, "build_id=%s\n", LOD3DS_BUILD_ID);
#else
	std::fprintf(manifest, "build_id=unavailable\n");
#endif
	std::fprintf(manifest, "sky_fallback.calls=%u\n",
		SkyFallbackCalls.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_fallback.filled_pixels=%u\n",
		SkyFallbackFilledPixels.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_fallback.last_fill_pixels=%u\n",
		SkyFallbackLastFilledPixels.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_column_repair.columns=%u\n",
		SkyColumnRepairColumns.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_column_repair.pixels=%u\n",
		SkyColumnRepairPixels.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_column_repair.last_columns=%u\n",
		SkyColumnRepairLastColumns.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_column_repair.last_pixels=%u\n",
		SkyColumnRepairLastPixels.load(std::memory_order_relaxed));
	std::fprintf(manifest, "flat_sky.background_calls=%u\n",
		FlatSkyBackgroundCalls.load(std::memory_order_relaxed));
	std::fprintf(manifest, "flat_sky.background_pixels=%u\n",
		FlatSkyBackgroundPixels.load(std::memory_order_relaxed));
	std::fprintf(manifest, "flat_sky.portal_planes_skipped=%u\n",
		FlatSkyPortalPlanesSkipped.load(std::memory_order_relaxed));
	std::fprintf(manifest, "sky_viewpoint.portal_planes_skipped=%llu\n",
		static_cast<unsigned long long>(
			SkyViewpointPortalPlanesSkipped.load(std::memory_order_relaxed)));
	std::fprintf(manifest,
		"draw_distance.max_units=2048 fade_start_units=1536 bsp_subtrees_culled=%llu "
		"lines_culled=%llu sprites_culled=%llu fog_pixels=%llu\n",
		static_cast<unsigned long long>(
			DrawDistanceBspSubtreesCulled.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			DrawDistanceLinesCulled.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			DrawDistanceSpritesCulled.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(
			DrawDistanceFogPixels.load(std::memory_order_relaxed)));
	#if defined(LOD3DS_HYBRID_PERFORMANCE)
	std::fprintf(manifest, "runtime.resolution_percent=%d width=%d height=%d bilinear=yes\n",
		I_3DSGameplayResolutionTenths() * 10, I_3DSGameplayResolutionWidth(),
		I_3DSGameplayResolutionHeight());
	#else
	std::fprintf(manifest, "runtime.resolution_percent=100 width=320 height=200 bilinear=yes\n");
	#endif
	std::fprintf(manifest, "runtime.fps_limit=%d\n", static_cast<int>(vid_maxfps));
	std::fprintf(manifest, "audio.dump_output=paused-entire-openal-device\n");
	std::fprintf(manifest, "timestamp_ms=%llu system_tick=%llu\n",
		static_cast<unsigned long long>(osGetTime()),
		static_cast<unsigned long long>(svcGetSystemTick()));
	std::fprintf(manifest, "hardware=%s model_query_result=%08lx\n",
		R_SUCCEEDED(modelResult) ? (isNew3DS ? "new3ds" : "old3ds") : "unknown",
		static_cast<unsigned long>(modelResult));
	std::fprintf(manifest, "kernel=%08lx firm=%08lx system_core=%08lx\n",
		static_cast<unsigned long>(osGetKernelVersion()),
		static_cast<unsigned long>(osGetFirmVersion()),
		static_cast<unsigned long>(osGetSystemCoreVersion()));
	std::fprintf(manifest, "thread_id=%lu thread_priority=%ld\n",
		static_cast<unsigned long>(threadId), static_cast<long>(threadPriority));
	std::fprintf(manifest,
		"launch=%s requested_heap=%lu requested_linear_heap=%lu held_buttons=%08lx "
		"slider_3d=%.6f\n",
		envIsHomebrew() ? "3dsx" : "cia", static_cast<unsigned long>(envGetHeapSize()),
		static_cast<unsigned long>(envGetLinearHeapSize()),
		static_cast<unsigned long>(HeldButtons), static_cast<double>(osGet3DSliderState()));
	std::fprintf(manifest, "application_memory_free=%lu application_memory_size=%lu\n",
		static_cast<unsigned long>(osGetMemRegionFree(MEMREGION_APPLICATION)),
		static_cast<unsigned long>(osGetMemRegionSize(MEMREGION_APPLICATION)));
	std::fprintf(manifest, "linear_free=%lu heap_arena=%d heap_used=%d heap_free=%d\n",
		static_cast<unsigned long>(linearSpaceFree()), heap.arena, heap.uordblks, heap.fordblks);
	std::fprintf(manifest,
		"citro3d_processing_ms=%.6f citro3d_drawing_ms=%.6f citro3d_cmd_buffer_usage=%.6f\n",
		static_cast<double>(C3D_GetProcessingTime()), static_cast<double>(C3D_GetDrawingTime()),
		static_cast<double>(C3D_GetCmdBufUsage()));
	std::fprintf(manifest, "sd_free_before=%s%llu reserve=%llu\n",
		freeBeforeKnown ? "" : "unknown/", static_cast<unsigned long long>(freeBefore),
		static_cast<unsigned long long>(FreeSpaceReserveBytes));
	std::fprintf(manifest,
		"privacy=no ROM, IWAD, PK3, save, demo, environment, or arbitrary external file is copied. "
		"boot.log is secret-redacted and the INI is allowlisted. Only a full dump copies process "
		"memory; it can inherently contain transient bytes of loaded resources.\n");
	std::fflush(manifest);

	// Persist the immutable RAM copies. The resulting images still show the
	// exact pre-progress frame even though the user can already see this work.
	const bool topOk = WriteScreenCapture(manifest, directory, topSnapshot, "top");
	const bool bottomOk = WriteScreenCapture(manifest, directory, bottomSnapshot, "bottom");
	topSnapshot.Release();
	bottomSnapshot.Release();
	if (!full) DiagnosticProgressValue = 30;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage), "SAVING GAME STATE");
	DrawBottomOverlay(true);

	char destination[384] = {};
	size_t logBytes = 0;
	unsigned redactedLines = 0;
	FormatPath(destination, sizeof(destination), "%s/boot-sanitized.log", directory);
	const bool logExists = PathExists(BootLogPath);
	const bool logOk = logExists && CopySanitizedBootLog(destination, logBytes, redactedLines);
	std::fprintf(manifest, "boot_log=%s bytes=%lu redacted_lines=%u\n",
		logOk ? "ok" : (logExists ? "failed" : "unavailable"),
		static_cast<unsigned long>(logBytes), redactedLines);

	unsigned configKeys = 0;
	unsigned skippedConfigLines = 0;
	FormatPath(destination, sizeof(destination), "%s/config-sanitized.txt", directory);
	const bool configExists = PathExists(ConfigPath);
	const bool configOk = configExists &&
		WriteSanitizedConfig(destination, configKeys, skippedConfigLines);
	std::fprintf(manifest, "config=%s allowlisted_keys=%u omitted_lines=%u\n",
		configOk ? "ok" : (configExists ? "failed" : "unavailable"),
		configKeys, skippedConfigLines);

	FormatPath(destination, sizeof(destination), "%s/engine-state.txt", directory);
	const bool engineStateOk = I_3DSWriteEngineDiagnosticSnapshot(destination);
	std::fprintf(manifest, "engine_state=%s\n", engineStateOk ? "ok" : "failed");
	if (!full) DiagnosticProgressValue = 60;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage), "COPYING LOGS");
	DrawBottomOverlay(true);

	bool artifactsOk = true;
	const char *manifestSource = PathExists(BuildManifestPath) ? BuildManifestPath :
		RomfsBuildManifestPath;
	artifactsOk &= CopyDiagnosticArtifact(manifest, directory, "build_manifest", manifestSource,
		"BUILD-MANIFEST.txt", 512u * 1024u);
	artifactsOk &= CopyDiagnosticArtifact(manifest, directory, "startup_log", StartupLogPath,
		"startup.log", MaxCopiedLogBytes);
	artifactsOk &= CopyDiagnosticArtifact(manifest, directory, "fatal_log", FatalLogPath,
		"fatal.log", MaxCopiedLogBytes);
	#if defined(LOD3DS_HARDWARE_DIAGNOSTIC) && LOD3DS_HARDWARE_DIAGNOSTIC
	// Persist the in-RAM segment immediately before copying it into a dump. The
	// render loop normally writes only once per 720 frames, avoiding SD open /
	// flush / close latency in every measured frame.
	FlushFrameTelemetry();
	#endif
	artifactsOk &= CopyDiagnosticArtifact(manifest, directory, "frame_telemetry", TelemetryPath,
		"frame-telemetry.csv", MaxCopiedTelemetryBytes);
	artifactsOk &= CopyDiagnosticArtifact(manifest, directory, "novagl_lite", NovaLitePath,
		"nova-lite.csv", MaxCopiedTelemetryBytes);
	artifactsOk &= CopyDiagnosticArtifact(manifest, directory, "novagl_log", NovaGLLogPath,
		"novagl.log", MaxCopiedRendererLogBytes);
	std::fflush(manifest);
	if (!full) DiagnosticProgressValue = 85;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage),
		full ? "PREPARING MEMORY" : "SAVING MEMORY MAP");
	DrawBottomOverlay(true);

	const FMemoryDumpResult memory = full ? DumpReadableMemory(manifest, directory) :
		WriteMemorySurvey(manifest, directory);
	if (!full) DiagnosticProgressValue = 100;
	std::snprintf(DiagnosticProgressStage, sizeof(DiagnosticProgressStage), "FINALIZING");
	DrawBottomOverlay(true);
	uint64_t freeAfter = 0;
	const bool freeAfterKnown = QueryFreeSpace(freeAfter);
	const bool optionalFilesOk = (!logExists || logOk) && (!configExists || configOk) &&
		artifactsOk;
	const bool complete = topOk && bottomOk && engineStateOk && optionalFilesOk && memory.Complete;
	std::fprintf(manifest, "sd_free_after=%s%llu\n", freeAfterKnown ? "" : "unknown/",
		static_cast<unsigned long long>(freeAfter));
	std::fprintf(manifest, "final_status=%s\n", complete ? "complete" : "incomplete");
	const bool manifestOk = FinishAtomicFile(manifest, manifestPartialPath, manifestPath);

	const bool finalComplete = complete && manifestOk;
	char marker[512] = {};
	std::snprintf(marker, sizeof(marker),
		"status=%s\nmode=%s\nmanifest=%s\nmemory_bytes=%llu/%llu\n",
		finalComplete ? "complete" : "incomplete", DumpModeName(mode),
		manifestOk ? "manifest.txt" : "failed",
		static_cast<unsigned long long>(memory.WrittenBytes),
		static_cast<unsigned long long>(memory.ExpectedBytes));
	WriteMarker(directory, finalComplete ? "COMPLETE" : "INCOMPLETE.txt", marker);

	std::fprintf(stdout, "[lod3ds] %s diagnostic dump %s: %s\n", DumpModeName(mode),
		finalComplete ? "complete" : "incomplete", directory);
	std::fflush(stdout);
	DiagnosticProgressActive = false;
	SetOverlayNotification(finalComplete ?
		(full ? "FULL DUMP COMPLETE" : "QUICK DUMP COMPLETE") : "DUMP INCOMPLETE");
	DrawBottomOverlay(true);
}
}

bool I_3DSNativeMenuVisible()
{
	return !DiagnosticProgressActive && menuactive != MENU_Off && CurrentMenu != nullptr;
}

void I_3DSRouteNativeMenuFrame(unsigned char *menuPixels,
	const unsigned char *basePixels, int pitchBytes, int width, int height)
{
	if (menuPixels == nullptr || basePixels == nullptr || pitchBytes < width * 4 ||
		width <= 0 || height <= 0)
	{
		return;
	}

	u16 physicalWidth = 0;
	u16 physicalHeight = 0;
	unsigned char *bottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT,
		&physicalWidth, &physicalHeight);
	if (bottom != nullptr && physicalWidth == BottomScreenHeight &&
		physicalHeight == BottomScreenWidth)
	{
		DrawNativeMenuBottomFrame(bottom, menuPixels, basePixels,
			pitchBytes, width, height);
		I_3DSCleanDataCache(bottom, BottomScreenWidth * BottomScreenHeight * 4u);
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
		BottomPresentation = EBottomPresentation::NativeMenu;
		OverlayLastDrawMilliseconds = osGetTime();
	}

	static std::vector<unsigned char> saveLoadSource;
	if (NativeSaveLoadMenuVisible())
	{
		const size_t byteCount = static_cast<size_t>(pitchBytes) *
			static_cast<size_t>(height);
		saveLoadSource.assign(menuPixels, menuPixels + byteCount);
	}
	else
	{
		saveLoadSource.clear();
	}
	std::memcpy(menuPixels, basePixels,
		static_cast<size_t>(pitchBytes) * static_cast<size_t>(height));
	ComposeNativeMenuTop(menuPixels, pitchBytes, width, height,
		saveLoadSource.empty() ? nullptr : saveLoadSource.data(), basePixels);
}

void I_3DSComposeGameplayFrame(unsigned char *pixels, int pitchBytes,
	int width, int height)
{
	if (pixels == nullptr || pitchBytes < width * 4 || width <= 0 || height <= 0 ||
		gamestate != GS_LEVEL || menuactive != MENU_Off)
	{
		return;
	}

	auto putPixel = [&](int x, int y, FOverlayColor color, unsigned alpha = 255u)
	{
		if (x < 0 || x >= width || y < 0 || y >= height || alpha == 0u) return;
		unsigned char *target = pixels + static_cast<size_t>(y) * pitchBytes + x * 4;
		const unsigned source[] = { color.Blue, color.Green, color.Red };
		for (int channel = 0; channel < 3; ++channel)
		{
			target[channel] = static_cast<unsigned char>((source[channel] * alpha +
				target[channel] * (255u - alpha) + 127u) / 255u);
		}
		target[3] = 255u;
	};
	auto fillRect = [&](int x, int y, int rectWidth, int rectHeight,
		FOverlayColor color)
	{
		for (int py = std::max(0, y); py < std::min(height, y + rectHeight); ++py)
			for (int px = std::max(0, x); px < std::min(width, x + rectWidth); ++px)
				putPixel(px, py, color);
	};

	if (automapactive)
	{
		// Use a wider terrain overview for the upper automap.
		static std::array<unsigned char,
			BottomScreenWidth * BottomScreenHeight * 4u> mapPixels{};
		mapPixels.fill(0u);
		constexpr int SourceWidth = 320;
		constexpr int SourceHeight = 192;
		DrawBottomAutomap(mapPixels.data(), 0, 0, SourceWidth, SourceHeight, 64.0);
		for (int y = 0; y < height; ++y)
		{
			const int sourceY = y * SourceHeight / height;
			for (int x = 0; x < width; ++x)
			{
				const int sourceX = x * SourceWidth / width;
				const size_t sourceOffset = 4u * (
					static_cast<size_t>(sourceX) * BottomScreenHeight +
					(BottomScreenHeight - 1u - static_cast<unsigned>(sourceY)));
				putPixel(x, y, FOverlayColor{
					mapPixels[sourceOffset + 3], mapPixels[sourceOffset + 2],
					mapPixels[sourceOffset + 1] });
			}
		}
		return;
	}

	if (vid_fps)
	{
		char label[32] = {};
		std::snprintf(label, sizeof(label), "FPS %llu  %.1fMS",
			static_cast<unsigned long long>(std::min<uint64_t>(LastCount, 999u)),
			std::clamp(LastFrameMilliseconds, 0.0, 999.9));
		const bool compactGlyphs = width <= 200;
		const int glyphWidth = compactGlyphs ? 3 : 5;
		const int glyphHeight = compactGlyphs ? 5 : 7;
		const int scale = width >= 400 ? 2 : 1;
		const int advance = (glyphWidth + 1) * scale;
		const int boxWidth = static_cast<int>(std::strlen(label)) * advance + 7;
		const int boxHeight = glyphHeight * scale + 6;
		const int boxX = std::max(2, width - boxWidth - 4);
		const int boxY = std::max(2, height / 60);
		fillRect(boxX, boxY, boxWidth, boxHeight, OverlayInk);
		int textX = boxX + 4;
		for (const char *cursor = label; *cursor != '\0'; ++cursor,
			textX += advance)
		{
			const uint8_t *rows = LoadingFindGlyph(*cursor);
			if (rows == nullptr) continue;
			for (int row = 0; row < glyphHeight; ++row)
				for (int column = 0; column < glyphWidth; ++column)
				{
					const int sourceRow = row * 7 / glyphHeight;
					const int sourceColumn = column * 5 / glyphWidth;
					if ((rows[sourceRow] & (1u << (4 - sourceColumn))) != 0u)
						fillRect(textX + column * scale, boxY + 3 + row * scale,
							scale, scale, OverlayIvory);
				}
		}
	}

	AActor *camera = players[consoleplayer].camera;
	if (!crosshairon || camera == nullptr || camera->health <= 0 ||
		(players[consoleplayer].cheats & CF_CHASECAM))
	{
		return;
	}
	const int targetSize = std::clamp(
		(static_cast<int>(AimCrosshairWidth) * width * 7 + 2000) / 4000,
		6, 12);
	const int left = width / 2 - targetSize / 2;
	// Keep the approved 70% size, but raise the marker four pixels from v0.28.
	const int verticalOffset = (6 * height + 120) / 240;
	const int top = height / 2 - targetSize / 2 + verticalOffset;
	for (int y = 0; y < targetSize; ++y)
	{
		const int sourceY = y * AimCrosshairHeight / targetSize;
		for (int x = 0; x < targetSize; ++x)
		{
			const int sourceX = x * AimCrosshairWidth / targetSize;
			const unsigned alpha = AimCrosshairAlpha[
				sourceY * AimCrosshairWidth + sourceX];
			if (alpha < 8u) continue;
			putPixel(left + x + 1, top + y + 1, OverlayInk, alpha * 3u / 5u);
			putPixel(left + x, top + y, OverlayIvory, alpha);
		}
	}
}

void I_3DSRecordSkyFallback(unsigned int filledPixels)
{
	SkyFallbackCalls.fetch_add(1, std::memory_order_relaxed);
	SkyFallbackFilledPixels.fetch_add(filledPixels, std::memory_order_relaxed);
	SkyFallbackLastFilledPixels.store(filledPixels, std::memory_order_relaxed);
}

void I_3DSRecordSkyColumnRepair(unsigned int repairedColumns,
	unsigned int repairedPixels)
{
	SkyColumnRepairColumns.fetch_add(repairedColumns, std::memory_order_relaxed);
	SkyColumnRepairPixels.fetch_add(repairedPixels, std::memory_order_relaxed);
	SkyColumnRepairLastColumns.store(repairedColumns, std::memory_order_relaxed);
	SkyColumnRepairLastPixels.store(repairedPixels, std::memory_order_relaxed);
}

void I_3DSRecordFlatSkyBackground(unsigned int filledPixels)
{
	FlatSkyBackgroundCalls.fetch_add(1, std::memory_order_relaxed);
	FlatSkyBackgroundPixels.fetch_add(filledPixels, std::memory_order_relaxed);
}

void I_3DSRecordFlatSkyPortalSkip()
{
	FlatSkyPortalPlanesSkipped.fetch_add(1, std::memory_order_relaxed);
}

void I_3DSRecordSkyViewpointPortalSkip()
{
	SkyViewpointPortalPlanesSkipped.fetch_add(1, std::memory_order_relaxed);
}

void I_3DSRecordDrawDistanceBspCull()
{
	DrawDistanceBspSubtreesCulled.fetch_add(1, std::memory_order_relaxed);
}

void I_3DSRecordDrawDistanceLineCull()
{
	DrawDistanceLinesCulled.fetch_add(1, std::memory_order_relaxed);
}

void I_3DSRecordDrawDistanceSpriteCull()
{
	DrawDistanceSpritesCulled.fetch_add(1, std::memory_order_relaxed);
}

void I_3DSRecordDrawDistanceFog(unsigned int filledPixels)
{
	DrawDistanceFogPixels.fetch_add(filledPixels, std::memory_order_relaxed);
}

void I_3DSStartupLog(const char *stage)
{
	static bool firstCheckpoint = true;
	static bool linearPoolInitialized = false;
	if (stage == nullptr) stage = "(null)";

	// libctru initializes the linear allocator lazily. Until its first
	// allocation linearSpaceFree() reports zero even when the reserved arena is
	// healthy, which made the old diagnostics falsely report linear exhaustion.
	if (!linearPoolInitialized)
	{
		void *probe = linearAlloc(128);
		if (probe != nullptr) linearFree(probe);
		linearPoolInitialized = true;
	}

	if (!EnsureDirectory(AppDirectory)) return;
	FILE *log = std::fopen(StartupLogPath, firstCheckpoint ? "w" : "a");
	if (log == nullptr) return;
	firstCheckpoint = false;

	const struct mallinfo heap = mallinfo();
	std::fprintf(log,
		"%llu tick=%llu build=%s version=%s profile=%s stage=%s launch=%s app_free=%lu app_total=%lu "
		"heap_requested=%lu linear_requested=%lu heap_used=%d heap_free=%d "
		"linear_free=%lu vram_free=%lu\n",
		static_cast<unsigned long long>(osGetTime()),
		static_cast<unsigned long long>(svcGetSystemTick()), LOD3DS_BUILD_ID,
		LOD3DS_PORT_VERSION, LOD3DS_BUILD_PROFILE_NAME, stage,
		envIsHomebrew() ? "3dsx" : "cia",
		static_cast<unsigned long>(osGetMemRegionFree(MEMREGION_APPLICATION)),
		static_cast<unsigned long>(osGetMemRegionSize(MEMREGION_APPLICATION)),
		static_cast<unsigned long>(envGetHeapSize()),
		static_cast<unsigned long>(envGetLinearHeapSize()),
		heap.uordblks, heap.fordblks,
		static_cast<unsigned long>(linearSpaceFree()),
		static_cast<unsigned long>(vramSpaceFree()));
	std::fflush(log);
	std::fclose(log);

	// Startup progress is intentionally log-only on real hardware. Direct CPU
	// writes to SDL's early VRAM framebuffer caused a permission data abort on a
	// New 3DS before the renderer opened (Luma dump 00000101).
}

void I_3DSLoadingScreenStart()
{
	if (LoadingAnimationThread != nullptr) return;
	LoadingScreenActive = true;
	LoadingScreenFinished = false;
	LoadingAnimationStop.store(false, std::memory_order_release);
	// Put the first frame on the LCD synchronously so opening the title never
	// resembles a dead black screen while the worker is being scheduled.
	if (!DrawTriforceAnimationFrame(0)) return;
	gspWaitForVBlank();
	LoadingAnimationThread = threadCreate(TriforceAnimationMain, nullptr,
		LoadingAnimationStackBytes, 0x31, -1, false);
	I_3DSStartupLog(LoadingAnimationThread != nullptr ?
		"loading-animation-ready" : "loading-animation-static-only");
}

void I_3DSLoadingScreenFinish()
{
	if (LoadingScreenFinished) return;
	LoadingScreenFinished = true;
	LoadingScreenActive = false;
	LoadingAnimationStop.store(true, std::memory_order_release);
	if (LoadingAnimationThread != nullptr)
	{
		threadJoin(LoadingAnimationThread, U64_MAX);
		threadFree(LoadingAnimationThread);
		LoadingAnimationThread = nullptr;
	}
	I_3DSStartupLog("loading-animation-finished");
}

void I_3DSPrepareNativeKeyboardTop()
{
	constexpr size_t TopFramebufferBytes = 400u * 240u * 4u;
	u16 physicalWidth = 0;
	u16 physicalHeight = 0;
	unsigned char *first = gfxGetFramebuffer(GFX_TOP, GFX_LEFT,
		&physicalWidth, &physicalHeight);
	if (first == nullptr || physicalWidth != 240 || physicalHeight != 400) return;

	std::vector<unsigned char> firstImage(first, first + TopFramebufferBytes);
	I_3DSCleanDataCache(first, TopFramebufferBytes);
	gfxScreenSwapBuffers(GFX_TOP, false);
	gspWaitForVBlank();

	unsigned char *second = gfxGetFramebuffer(GFX_TOP, GFX_LEFT,
		&physicalWidth, &physicalHeight);
	if (second == nullptr || physicalWidth != 240 || physicalHeight != 400) return;
	std::vector<unsigned char> secondImage(second, second + TopFramebufferBytes);

	// The gfx API exposes the next draw buffer, while the scene may live in the
	// other half of the double buffer. Prefer the copy with actual RGB content;
	// this avoids preserving a freshly cleared black surface.
	auto imageScore = [](const std::vector<unsigned char> &image)
	{
		uint64_t score = 0;
		for (size_t offset = 0; offset + 3 < image.size(); offset += 4)
			score += static_cast<uint64_t>(image[offset + 1]) +
				image[offset + 2] + image[offset + 3];
		return score;
	};
	const std::vector<unsigned char> &chosen =
		imageScore(secondImage) > imageScore(firstImage) ? secondImage : firstImage;

	std::memcpy(second, chosen.data(), TopFramebufferBytes);
	I_3DSCleanDataCache(second, TopFramebufferBytes);
	gfxScreenSwapBuffers(GFX_TOP, false);
	gspWaitForVBlank();

	first = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &physicalWidth, &physicalHeight);
	if (first != nullptr && physicalWidth == 240 && physicalHeight == 400)
	{
		std::memcpy(first, chosen.data(), TopFramebufferBytes);
		I_3DSCleanDataCache(first, TopFramebufferBytes);
	}
}

void I_3DSFrameTelemetryBegin()
{
#if defined(LOD3DS_HARDWARE_DIAGNOSTIC) && LOD3DS_HARDWARE_DIAGNOSTIC
	osTickCounterStart(&FrameTelemetryTimer);
	FrameTelemetryRunning = true;
	FrameTelemetryDraws = FrameTelemetryVertices = FrameTelemetryTriangles = 0;
	FrameTelemetryFans = FrameTelemetryStrips = FrameTelemetryIndexed = 0;
#endif
}

void I_3DSFrameTelemetryDraw(unsigned int topology, unsigned int vertices, bool indexed)
{
#if defined(LOD3DS_HARDWARE_DIAGNOSTIC) && LOD3DS_HARDWARE_DIAGNOSTIC
	if (!FrameTelemetryRunning) return;
	++FrameTelemetryDraws;
	FrameTelemetryVertices += vertices;
	if (indexed) ++FrameTelemetryIndexed;
	// FRenderState topology ids: triangles=2, fan=3, strip=4.
	if (topology == 2) ++FrameTelemetryTriangles;
	else if (topology == 3) ++FrameTelemetryFans;
	else if (topology == 4) ++FrameTelemetryStrips;
#else
	(void)topology; (void)vertices; (void)indexed;
#endif
}

void I_3DSFrameTelemetryEnd()
{
#if defined(LOD3DS_HARDWARE_DIAGNOSTIC) && LOD3DS_HARDWARE_DIAGNOSTIC
	if (!FrameTelemetryRunning) return;
	FrameTelemetryRunning = false;
	osTickCounterUpdate(&FrameTelemetryTimer);

	const bool newSegment = (FrameTelemetrySerial % TelemetryFramesPerSegment) == 0;
	if (newSegment)
	{
		if (FrameTelemetrySerial != 0) FlushFrameTelemetry();
		FrameTelemetryBufferUsed = 0;
		const int header = std::snprintf(FrameTelemetryBuffer, FrameTelemetryBufferBytes,
			"build_id,profile,hardware_target,frame,timestamp_ms,"
			"render_present_ms,citro_cpu_ms,citro_gpu_ms,"
			"draw_calls,input_vertices,triangle_draws,fan_draws,strip_draws,indexed_draws,"
			"linear_free_bytes,vram_free_bytes,heap_used_bytes,heap_free_bytes\n");
		if (header > 0) FrameTelemetryBufferUsed = static_cast<size_t>(header);
	}
	const struct mallinfo heap = mallinfo();
	char row[384] = {};
	const int rowBytes = std::snprintf(row, sizeof(row),
		"%s,%s,New Nintendo 3DS,%llu,%llu,"
		"%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,%u,%lu,%lu,%d,%d\n",
		LOD3DS_BUILD_ID,
		LOD3DS_BUILD_PROFILE_NAME,
		FrameTelemetrySerial,
		static_cast<unsigned long long>(osGetTime()),
		static_cast<double>(osTickCounterRead(&FrameTelemetryTimer)),
		static_cast<double>(C3D_GetProcessingTime()),
		static_cast<double>(C3D_GetDrawingTime()),
		FrameTelemetryDraws, FrameTelemetryVertices, FrameTelemetryTriangles,
		FrameTelemetryFans, FrameTelemetryStrips, FrameTelemetryIndexed,
		static_cast<unsigned long>(linearSpaceFree()),
		static_cast<unsigned long>(vramSpaceFree()),
		heap.uordblks, heap.fordblks);
	if (rowBytes > 0 && static_cast<size_t>(rowBytes) < sizeof(row) &&
		FrameTelemetryBufferUsed + static_cast<size_t>(rowBytes) <= FrameTelemetryBufferBytes)
	{
		std::memcpy(FrameTelemetryBuffer + FrameTelemetryBufferUsed, row,
			static_cast<size_t>(rowBytes));
		FrameTelemetryBufferUsed += static_cast<size_t>(rowBytes);
	}
	++FrameTelemetrySerial;
#endif
}

void I_3DSWriteFatalLog(const char *message)
{
	I_3DSStartupLog("fatal-error");
	if (!EnsureDirectory(AppDirectory)) return;
	FILE *log = std::fopen(FatalLogPath, "w");
	if (log == nullptr) return;
	std::fprintf(log, "%s\n", message != nullptr ? message : "Unknown fatal error");
	std::fflush(log);
	std::fclose(log);
}

F3DSDiagnosticButtonResult I_3DSDiagnosticButtonEvent(unsigned int button, bool pressed)
{
	F3DSDiagnosticButtonResult result;
	if (button >= 32) return result;

	const uint32_t bit = 1u << button;
	if (pressed) HeldButtons |= bit;
	else HeldButtons &= ~bit;

	if (button == 2u && lod3ds_select_overlay && gamestate == GS_LEVEL &&
		(menuactive == MENU_Off || DeveloperOverlayVisible))
	{
		if (pressed)
		{
			DeveloperOverlayVisible = !DeveloperOverlayVisible;
			DrawBottomOverlay(true);
		}
		result.SuppressEvent = true;
		return result;
	}

	if (SuppressedComboMask == 0)
	{
		if ((HeldButtons & CleanDumpButtonMask) == CleanDumpButtonMask)
		{
			I_3DSRequestCleanDiagnosticDumps();
			SuppressedComboMask = CleanDumpButtonMask;
			result.ReleaseComboKeys = true;
		}
		else if ((HeldButtons & FullDumpButtonMask) == FullDumpButtonMask)
		{
			I_3DSRequestFullDiagnosticDump();
			SuppressedComboMask = FullDumpButtonMask;
			result.ReleaseComboKeys = true;
		}
		else if ((HeldButtons & QuickDumpButtonMask) == QuickDumpButtonMask)
		{
			I_3DSRequestDiagnosticDump();
			SuppressedComboMask = QuickDumpButtonMask;
			result.ReleaseComboKeys = true;
		}
	}

	if (SuppressedComboMask != 0 && (bit & SuppressedComboMask) != 0)
	{
		result.SuppressEvent = true;
		if ((HeldButtons & SuppressedComboMask) == 0)
		{
			SuppressedComboMask = 0;
		}
	}
	return result;
}

void RequestDiagnosticDump(EDiagnosticDumpMode mode)
{
	unsigned requested = DumpRequestedMode.load(std::memory_order_relaxed);
	const unsigned desired = static_cast<unsigned>(mode);
	const uint64_t notBefore = osGetTime() +
		(mode == EDiagnosticDumpMode::Clean ? 0 : DumpUiDelayMilliseconds);
	while (requested < desired)
	{
		// Publish the deadline before the request mode. The service's acquire load
		// then cannot observe a quick/full request with an uninitialized deadline.
		DumpRequestNotBeforeMilliseconds.store(notBefore, std::memory_order_relaxed);
		if (DumpRequestedMode.compare_exchange_weak(requested, desired,
			std::memory_order_release, std::memory_order_relaxed))
		{
			return;
		}
	}
}

void I_3DSRequestDiagnosticDump()
{
	RequestDiagnosticDump(EDiagnosticDumpMode::Quick);
}

void I_3DSRequestFullDiagnosticDump()
{
	RequestDiagnosticDump(EDiagnosticDumpMode::Full);
}

void I_3DSRequestCleanDiagnosticDumps()
{
	RequestDiagnosticDump(EDiagnosticDumpMode::Clean);
}

bool I_3DSDiagnosticTouch(float x, float y)
{
	if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) return false;
	if (I_3DSNativeMenuVisible())
	{
		const int touchY = static_cast<int>(y * BottomScreenHeight);
		for (unsigned row = 0; row < NativeMenuTouchRowCount; ++row)
		{
			if (touchY < NativeMenuTouchRows[row].Top ||
				touchY >= NativeMenuTouchRows[row].Bottom) continue;
			const int item = NativeMenuTouchRows[row].Item;
			if (NativeMenuCustomList && CurrentMenu->IsKindOf("ListMenu"))
			{
				DListMenuDescriptor *descriptor =
					CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc"));
				if (descriptor != nullptr && item >= 0 &&
					static_cast<unsigned>(item) < descriptor->mItems.Size())
				{
					descriptor->mSelectedItem = item;
					CurrentMenu->CallMenuEvent(MKEY_Enter, true);
				}
			}
			else if (NativeMenuCustomOption && CurrentMenu->IsKindOf("OptionMenu"))
			{
				DOptionMenuDescriptor *descriptor =
					CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
				if (descriptor != nullptr && item >= 0 &&
					static_cast<unsigned>(item) < descriptor->mItems.Size())
				{
					descriptor->mSelectedItem = item;
					CurrentMenu->CallMenuEvent(MKEY_Enter, true);
				}
			}
			else if (NativeMenuCustomSave && CurrentMenu->IsKindOf("LoadSaveMenu"))
			{
				CurrentMenu->IntVar(FName("Selected")) = item;
				CurrentMenu->CallMenuEvent(MKEY_Enter, true);
			}
			return true;
		}
		// Feed the exact inverse of the enlarged authentic-menu transform back to
		// the engine responder. Sliders, submenus and dialogs therefore retain the
		// original game's behavior even though they are larger on the touch LCD.
		if (NativeMenuTransformValid && NativeMenuScale > 0.0f)
		{
			const float pixelX = x * BottomScreenWidth;
			const float pixelY = y * BottomScreenHeight;
			event_t event = {};
			event.type = EV_GUI_Event;
			event.data1 = static_cast<int16_t>(std::clamp(
				static_cast<int>(NativeMenuSourceLeft +
					(pixelX - NativeMenuTargetLeft) / NativeMenuScale + 0.5f), 0, 399));
			event.data2 = static_cast<int16_t>(std::clamp(
				static_cast<int>(NativeMenuSourceTop +
					(pixelY - NativeMenuTargetTop) / NativeMenuScale + 0.5f), 0, 239));
			event.subtype = EV_GUI_LButtonDown;
			DMenu *pressedMenu = CurrentMenu;
			pressedMenu->CallResponder(&event);
			event.subtype = EV_GUI_LButtonUp;
			if (CurrentMenu != nullptr) CurrentMenu->CallResponder(&event);
		}
		return true;
	}

	if (DeveloperOverlayVisible && gamestate == GS_LEVEL)
	{
		const float pixelX = x * BottomScreenWidth;
		const float pixelY = y * BottomScreenHeight;
		if (pixelY >= 178.0f && pixelY < 222.0f)
		{
			if (pixelX >= 7.0f && pixelX < 106.0f) I_3DSRequestDiagnosticDump();
			else if (pixelX >= 110.0f && pixelX < 210.0f)
				I_3DSRequestFullDiagnosticDump();
			else if (pixelX >= 214.0f && pixelX < 314.0f)
				I_3DSRequestCleanDiagnosticDumps();
			DrawBottomOverlay(true);
		}
		return true;
	}

	// SDL reports touch coordinates normalized to the 320x240 lower LCD. The
	// bottom strip belongs to the gameplay tabs; the rest remains the relative
	// look surface so this UI does not take a movement control away from players.
	if (gamestate != GS_LEVEL)
	{
		return false;
	}

	const float pixelX = x * BottomScreenWidth;
	const float pixelY = y * BottomScreenHeight;
	if (BottomGameplayTab == EBottomGameplayTab::Map &&
		pixelX >= 31.0f && pixelX < 235.0f &&
		pixelY >= 38.0f && pixelY < 194.0f)
	{
		BottomMapZoomedOut = !BottomMapZoomedOut;
		DrawBottomOverlay(true);
		return true;
	}
	if (BottomGameplayTab == EBottomGameplayTab::Items &&
		pixelX >= 34.0f && pixelX < 230.0f &&
		pixelY >= 38.0f && pixelY < 202.0f)
	{
		const unsigned column = static_cast<unsigned>((pixelX - 34.0f) / 49.0f);
		const unsigned row = static_cast<unsigned>((pixelY - 38.0f) / 41.0f);
		if (BottomSelectItem(row * 4u + column)) DrawBottomOverlay(true);
		return true;
	}
	if (pixelY < 202.0f) return false;
	if (pixelX >= 31.0f && pixelX < 133.0f)
	{
		BottomGameplayTab = EBottomGameplayTab::Map;
		DrawBottomOverlay(true);
		return true;
	}
	if (pixelX >= 133.0f && pixelX < 235.0f)
	{
		BottomGameplayTab = EBottomGameplayTab::Items;
		DrawBottomOverlay(true);
		return true;
	}
	return false;
}

void I_3DSServiceDiagnosticDump()
{
	const EDiagnosticDumpMode pendingMode = static_cast<EDiagnosticDumpMode>(
		DumpRequestedMode.load(std::memory_order_acquire));
	if (pendingMode == EDiagnosticDumpMode::None) return;
	const uint64_t notBefore = DumpRequestNotBeforeMilliseconds.load(std::memory_order_acquire);
	if (pendingMode != EDiagnosticDumpMode::Clean &&
		(notBefore == 0 || osGetTime() < notBefore))
	{
		return;
	}
	const EDiagnosticDumpMode mode = static_cast<EDiagnosticDumpMode>(
		DumpRequestedMode.exchange(static_cast<unsigned>(EDiagnosticDumpMode::None),
			std::memory_order_acq_rel));
	if (mode == EDiagnosticDumpMode::None) return;
	bool expected = false;
	if (!DumpRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
	if (mode == EDiagnosticDumpMode::Clean) CleanAllDiagnosticDumps();
	else WriteDiagnosticDump(mode);
	DumpRunning.store(false, std::memory_order_release);
}

void I_3DSOverlayFrame()
{
	DrawBottomOverlay(false);
}

void I_3DSSetAudioReady(bool ready)
{
	OverlayAudioReady = ready;
	SetOverlayNotification(ready ? "AUDIO READY" : "AUDIO UNAVAILABLE");
	DrawBottomOverlay(true);
}

void I_3DSSetMenuStoryPage(bool story)
{
	MenuStoryPage = story;
}
