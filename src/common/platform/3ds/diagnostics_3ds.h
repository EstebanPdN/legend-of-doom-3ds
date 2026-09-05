#pragma once

struct F3DSDiagnosticButtonResult
{
	bool SuppressEvent = false;
	bool ReleaseComboKeys = false;
};

// Writes one immediately-flushed startup checkpoint to startup.log. This is
// intentionally independent from GZDoom's logfile so a crash or hang during
// early engine/video initialization still leaves a precise last-known stage.
void I_3DSStartupLog(const char *stage);
void I_3DSWriteFatalLog(const char *message);

// The 3DS video service is brought up before the expensive WAD/ZScript load.
// These functions draw a tiny framebuffer-only progress screen without
// creating a GZDoom window or a NovaGL context. The final image remains on the
// LCD until the first real GPU frame replaces it.
void I_3DSLoadingScreenStart();
void I_3DSLoadingScreenFinish();

// Duplicates the most recent upper LCD image into both scanout buffers before
// launching the system software keyboard, so the applet preserves the game
// scene instead of inheriting a cleared back buffer.
void I_3DSPrepareNativeKeyboardTop();

// Hardware-diagnostic builds record one bounded CSV segment with render,
// Citro3D CPU/GPU, and memory timings. Calls compile out at their call sites
// for release builds.
void I_3DSFrameTelemetryBegin();
void I_3DSFrameTelemetryEnd();
void I_3DSFrameTelemetryDraw(unsigned int topology, unsigned int vertices, bool indexed);

// Records the primary-sky background pass without doing any filesystem I/O.
// The counters are written into the next diagnostic dump so a hardware test
// can prove that the fallback actually ran and how many pixels it repaired.
void I_3DSRecordSkyFallback(unsigned int filledPixels);
void I_3DSRecordSkyColumnRepair(unsigned int repairedColumns,
	unsigned int repairedPixels);

// Legacy flat-sky counters retained for compatibility with earlier dumps.
void I_3DSRecordFlatSkyBackground(unsigned int filledPixels);
void I_3DSRecordFlatSkyPortalSkip();

// MAP01 keeps its classic SKYWW cloud texture, but not the remote
// SkyViewpoint scene. These counters make the distance policy observable in
// the next diagnostic dump without logging from renderer hot paths.
void I_3DSRecordSkyViewpointPortalSkip();
void I_3DSRecordDrawDistanceBspCull();
void I_3DSRecordDrawDistanceLineCull();
void I_3DSRecordDrawDistanceSpriteCull();
void I_3DSRecordDrawDistanceFog(unsigned int filledPixels);

// Called from the SDL event pump on the main thread. It only updates the
// combo state and latches a request; it never performs filesystem I/O.
F3DSDiagnosticButtonResult I_3DSDiagnosticButtonEvent(unsigned int button, bool pressed);

// Switches the level-only MAP/ITEMS tabs when the player touches their bottom
// strip. All other lower-screen touches remain ordinary game input.
bool I_3DSDiagnosticTouch(float x, float y);

// The engine still draws its real ListMenu/OptionMenu. The 3DS software
// presenter snapshots the pre-menu canvas, extracts the resulting native menu
// pixels for the lower LCD, then restores the upper canvas with only the
// translucent live/title background.
bool I_3DSNativeMenuVisible();
void I_3DSRouteNativeMenuFrame(unsigned char *menuPixels,
	const unsigned char *basePixels, int pitchBytes, int width, int height);

// Final CPU-canvas compositor. It makes FPS and the supplied crosshair
// independent of gameplay render scale and replaces the classic brown
// automap with an enlarged copy of the textured lower-screen map.
void I_3DSComposeGameplayFrame(unsigned char *pixels, int pitchBytes,
	int width, int height);

// Called from the main thread after input polling, before the next frame is
// rendered. A pending request is consumed at most once per chord press.
void I_3DSServiceDiagnosticDump();

// Refreshes the persistent lower-screen map/items interface. This is
// deliberately framebuffer-only: no SDL renderer or GPU context is allocated.
void I_3DSOverlayFrame();
void I_3DSSetAudioReady(bool ready);

// Keeps the independent bottom LCD synchronized with the engine's title-page
// sequence. Credit/lore pages use a black lower screen; the ordinary title
// restores the supplied menu art.
void I_3DSSetMenuStoryPage(bool story);

// Latches the same request as the hardware chord. This exists so automated
// emulator runs can exercise the complete dump writer even when their input
// driver cannot hold three ordinary keyboard keys simultaneously.
void I_3DSRequestDiagnosticDump();
void I_3DSRequestFullDiagnosticDump();
void I_3DSRequestCleanDiagnosticDumps();

// Writes engine-level state that is meaningful without symbolizing the raw
// process-memory capture (map, player/camera, input command and render state).
// Implemented in d_main.cpp, where the engine globals are already available.
bool I_3DSWriteEngineDiagnosticSnapshot(const char *path);
