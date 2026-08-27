#include "diagnostics_3ds.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "gitinfo.h"
#include "version.h"

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

constexpr uint32_t ComboButtonMask = (1u << 0) | (1u << 8) | (1u << 9); // A + R + L
constexpr uintptr_t UserAddressLimit = 0x40000000u;
constexpr size_t IoChunkBytes = 64u * 1024u;
constexpr size_t MemorySegmentBytes = 8u * 1024u * 1024u;
constexpr uint64_t FreeSpaceReserveBytes = 8u * 1024u * 1024u;
constexpr size_t MaxCopiedLogBytes = 4u * 1024u * 1024u;
constexpr size_t MaxCopiedTelemetryBytes = 16u * 1024u * 1024u;
constexpr size_t MaxCopiedRendererLogBytes = 64u * 1024u * 1024u;
constexpr size_t MaxConfigInputBytes = 2u * 1024u * 1024u;
constexpr unsigned MaxMemoryRegions = 2048;

std::atomic<bool> DumpRequested{false};
std::atomic<bool> DumpRunning{false};
std::atomic<unsigned> DumpSerial{0};
uint32_t HeldButtons;
bool SuppressComboUntilReleased;

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
	{ 'I', { 14, 4, 4, 4, 4, 4, 14 } },
	{ 'L', { 16, 16, 16, 16, 16, 16, 31 } },
	{ 'M', { 17, 27, 21, 21, 17, 17, 17 } },
	{ 'N', { 17, 25, 21, 19, 17, 17, 17 } },
	{ 'O', { 14, 17, 17, 17, 17, 17, 14 } },
	{ 'P', { 30, 17, 17, 30, 16, 16, 16 } },
	{ 'R', { 30, 17, 17, 30, 20, 18, 17 } },
	{ 'S', { 15, 16, 16, 14, 1, 1, 30 } },
	{ 'T', { 31, 4, 4, 4, 4, 4, 4 } },
	{ 'U', { 17, 17, 17, 17, 17, 17, 14 } },
	{ 'V', { 17, 17, 17, 17, 17, 10, 4 } },
	{ 'X', { 17, 17, 10, 4, 10, 17, 17 } },
	{ '%', { 17, 2, 4, 8, 16, 17, 0 } },
	{ '-', { 0, 0, 0, 31, 0, 0, 0 } },
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

bool CreateSessionDirectory(char *output, size_t outputSize)
{
	if (!EnsureDirectory(AppDirectory) || !EnsureDirectory(DumpDirectory)) return false;

	char timestamp[48] = {};
	MakeTimestamp(timestamp, sizeof(timestamp));
	const unsigned serial = DumpSerial.fetch_add(1, std::memory_order_relaxed);
	for (unsigned attempt = 0; attempt < 100; ++attempt)
	{
		if (!FormatPath(output, outputSize, "%s/dump-%s-%03u-%02u", DumpDirectory,
			timestamp, serial, attempt))
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

bool WriteScreenCapture(FILE *manifest, const char *directory, gfxScreen_t screen,
	const char *name)
{
	uint16_t width = 0;
	uint16_t height = 0;
	const unsigned char *framebuffer = gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);
	const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(screen);
	const unsigned bytesPerPixel = gspGetBytesPerPixel(format);
	if (framebuffer == nullptr || width == 0 || height == 0 || bytesPerPixel == 0)
	{
		std::fprintf(manifest, "screen.%s=unavailable\n", name);
		return false;
	}

	char rawPath[384] = {};
	char bmpPath[384] = {};
	FormatPath(rawPath, sizeof(rawPath), "%s/%s-framebuffer.bin", directory, name);
	FormatPath(bmpPath, sizeof(bmpPath), "%s/%s-screen.bmp", directory, name);
	const size_t rawSize = static_cast<size_t>(width) * height * bytesPerPixel;
	uint64_t hash = 0;
	size_t written = 0;
	const bool rawOk = WriteRangeAtomic(rawPath, reinterpret_cast<uintptr_t>(framebuffer), rawSize,
		hash, written);
	const bool bmpOk = WriteFramebufferBmp(bmpPath, framebuffer, width, height, format);

	std::fprintf(manifest,
		"screen.%s=physical:%ux%u logical:%ux%u format:%u bpp:%u raw:%s "
		"raw_bytes:%lu raw_fnv1a64:%016llx bmp:%s\n",
		name, width, height, height, width, static_cast<unsigned>(format), bytesPerPixel,
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
	std::fprintf(map,
		"# svcQueryMemory map. Dump policy: every readable user mapping except free, reserved and I/O.\n");
	for (unsigned index = 0; address < UserAddressLimit && index < MaxMemoryRegions; ++index)
	{
		const uintptr_t query = address;
		MemInfo info = {};
		PageInfo page = {};
		if (!NextMemoryRegion(address, info, page))
		{
			std::fprintf(map, "query_error address=%08lx result=stopped\n",
				static_cast<unsigned long>(query));
			result.Complete = false;
			break;
		}
		const bool dumpable = IsDumpableMemory(info);
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
		++result.Regions;
		if (dumpable)
		{
			result.DumpableBytes += info.size;
			++result.DumpableRegions;
		}
	}
	if (address < UserAddressLimit && result.Regions >= MaxMemoryRegions) result.Complete = false;
	std::fprintf(map, "survey regions=%u dumpable_regions=%u dumpable_bytes=%llu complete=%s\n\n",
		result.Regions, result.DumpableRegions,
		static_cast<unsigned long long>(result.DumpableBytes), result.Complete ? "yes" : "no");
	return result;
}

FMemoryDumpResult DumpReadableMemory(FILE *manifest, const char *directory)
{
	FMemoryDumpResult result;
	char memoryDirectory[384] = {};
	char mapPath[384] = {};
	char mapPartialPath[384] = {};
	if (!FormatPath(memoryDirectory, sizeof(memoryDirectory), "%s/memory", directory) ||
		!EnsureDirectory(memoryDirectory) ||
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

	uint64_t freeBefore = 0;
	const bool freeKnown = QueryFreeSpace(freeBefore);
	std::fprintf(map, "# Dump results\n");
	std::fprintf(map, "space_before=%s%llu reserve=%llu\n", freeKnown ? "" : "unknown/",
		static_cast<unsigned long long>(freeBefore),
		static_cast<unsigned long long>(FreeSpaceReserveBytes));

	bool allOk = survey.Complete;
	bool stop = false;
	uintptr_t address = 0;
	unsigned dumpableIndex = 0;
	for (unsigned regionIndex = 0;
		address < UserAddressLimit && regionIndex < MaxMemoryRegions && !stop; ++regionIndex)
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
		unsigned part = 0;
		while (regionWritten < info.size)
		{
			const size_t segmentSize = std::min(MemorySegmentBytes,
				static_cast<size_t>(info.size) - regionWritten);
			const uintptr_t segmentAddress = static_cast<uintptr_t>(info.base_addr) + regionWritten;
			char path[384] = {};
			if (!FormatPath(path, sizeof(path), "%s/r%03u-%08lx-%08lx-p%03u.bin",
				memoryDirectory, dumpableIndex, static_cast<unsigned long>(info.base_addr),
				static_cast<unsigned long>(info.size), part))
			{
				regionOk = false;
				stop = true;
				break;
			}
			uint64_t hash = 0;
			size_t segmentWritten = 0;
			const bool segmentOk = WriteRangeAtomic(path, segmentAddress, segmentSize, hash,
				segmentWritten);
			std::fprintf(map,
				"file=r%03u-%08lx-%08lx-p%03u.bin address=%08lx expected=%lu "
				"written=%lu fnv1a64=%016llx status=%s errno=%d\n",
				dumpableIndex, static_cast<unsigned long>(info.base_addr),
				static_cast<unsigned long>(info.size), part,
				static_cast<unsigned long>(segmentAddress),
				static_cast<unsigned long>(segmentSize),
				static_cast<unsigned long>(segmentWritten),
				static_cast<unsigned long long>(hash), segmentOk ? "ok" : "failed", errno);
			std::fflush(map);
			if (!segmentOk)
			{
				regionOk = false;
				stop = true;
				break;
			}
			regionWritten += segmentWritten;
			result.WrittenBytes += segmentWritten;
			++result.Files;
			++part;
		}
		std::fprintf(map, "region_result=%03u written=%lu expected=%lu status=%s\n",
			dumpableIndex, static_cast<unsigned long>(regionWritten),
			static_cast<unsigned long>(info.size), regionOk ? "ok" : "failed");
		if (regionOk) ++result.WrittenRegions;
		else allOk = false;
		++dumpableIndex;
	}
	if (stop) allOk = false;
	result.Complete = allOk && result.WrittenBytes == result.ExpectedBytes &&
		result.WrittenRegions == result.ExpectedRegions;

	uint64_t freeAfter = 0;
	const bool freeAfterKnown = QueryFreeSpace(freeAfter);
	std::fprintf(map,
		"summary expected_bytes=%llu written_bytes=%llu expected_regions=%u "
		"written_regions=%u files=%u space_after=%s%llu complete=%s\n",
		static_cast<unsigned long long>(result.ExpectedBytes),
		static_cast<unsigned long long>(result.WrittenBytes), result.ExpectedRegions,
		result.WrittenRegions, result.Files, freeAfterKnown ? "" : "unknown/",
		static_cast<unsigned long long>(freeAfter), result.Complete ? "yes" : "no");
	const bool mapOk = FinishAtomicFile(map, mapPartialPath, mapPath);
	if (!mapOk) result.Complete = false;

	std::fprintf(manifest,
		"memory.status=%s expected_bytes=%llu written_bytes=%llu expected_regions=%u "
		"written_regions=%u files=%u map=%s\n",
		result.Complete ? "complete" : "incomplete",
		static_cast<unsigned long long>(result.ExpectedBytes),
		static_cast<unsigned long long>(result.WrittenBytes), result.ExpectedRegions,
		result.WrittenRegions, result.Files, mapOk ? "ok" : "failed");
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

void WriteDiagnosticDump()
{
	char directory[320] = {};
	if (!CreateSessionDirectory(directory, sizeof(directory)))
	{
		std::fprintf(stderr, "[lod3ds] L+R+A dump failed: could not create %s\n", DumpDirectory);
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
		std::fprintf(stderr, "[lod3ds] L+R+A dump failed: cannot create manifest in %s\n",
			directory);
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
	std::fprintf(manifest, "format_version=1\n");
	std::fprintf(manifest, "trigger=L+R+A edge-triggered\n");
	std::fprintf(manifest, "execution=main thread, after SDL input polling, before frame render\n");
	std::fprintf(manifest, "engine_version=%s\n", GetVersionString());
	std::fprintf(manifest, "engine_git_hash=%s\n", GetGitHash());
	std::fprintf(manifest, "engine_git_time=%s\n", GetGitTime());
	std::fprintf(manifest, "compiler=%s\n", __VERSION__);
#ifdef LOD3DS_BUILD_ID
	std::fprintf(manifest, "build_id=%s\n", LOD3DS_BUILD_ID);
#else
	std::fprintf(manifest, "build_id=unavailable\n");
#endif
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
		"boot.log is secret-redacted and the INI is allowlisted. Full process-memory diagnostics "
		"can inherently contain transient bytes of resources already loaded by the engine.\n");
	std::fflush(manifest);

	const bool topOk = WriteScreenCapture(manifest, directory, GFX_TOP, "top");
	const bool bottomOk = WriteScreenCapture(manifest, directory, GFX_BOTTOM, "bottom");

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

	const FMemoryDumpResult memory = DumpReadableMemory(manifest, directory);
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
		"status=%s\nmanifest=%s\nmemory_bytes=%llu/%llu\n",
		finalComplete ? "complete" : "incomplete", manifestOk ? "manifest.txt" : "failed",
		static_cast<unsigned long long>(memory.WrittenBytes),
		static_cast<unsigned long long>(memory.ExpectedBytes));
	WriteMarker(directory, finalComplete ? "COMPLETE" : "INCOMPLETE.txt", marker);

	std::fprintf(stdout, "[lod3ds] L+R+A diagnostic dump %s: %s\n",
		finalComplete ? "complete" : "incomplete", directory);
	std::fflush(stdout);
}
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
		"%llu tick=%llu stage=%s launch=%s app_free=%lu app_total=%lu "
		"heap_requested=%lu linear_requested=%lu heap_used=%d heap_free=%d "
		"linear_free=%lu vram_free=%lu\n",
		static_cast<unsigned long long>(osGetTime()),
		static_cast<unsigned long long>(svcGetSystemTick()), stage,
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
	// Kept as an ABI-compatible no-op for callers from older integration code.
}

void I_3DSLoadingScreenFinish()
{
	// Kept as an ABI-compatible no-op for callers from older integration code.
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
		"%s,hardware-diagnostic,New Nintendo 3DS,%llu,%llu,"
		"%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,%u,%lu,%lu,%d,%d\n",
		LOD3DS_BUILD_ID,
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

	const bool comboButton = (bit & ComboButtonMask) != 0;
	const bool comboHeld = (HeldButtons & ComboButtonMask) == ComboButtonMask;
	if (comboHeld && !SuppressComboUntilReleased)
	{
		I_3DSRequestDiagnosticDump();
		SuppressComboUntilReleased = true;
		result.ReleaseComboKeys = true;
	}

	if (SuppressComboUntilReleased && comboButton)
	{
		result.SuppressEvent = true;
		if ((HeldButtons & ComboButtonMask) == 0)
		{
			SuppressComboUntilReleased = false;
		}
	}
	return result;
}

void I_3DSRequestDiagnosticDump()
{
	DumpRequested.store(true, std::memory_order_release);
}

void I_3DSServiceDiagnosticDump()
{
	if (!DumpRequested.exchange(false, std::memory_order_acq_rel)) return;
	bool expected = false;
	if (!DumpRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
	WriteDiagnosticDump();
	DumpRunning.store(false, std::memory_order_release);
}
