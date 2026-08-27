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

// Hardware-diagnostic builds record one bounded CSV segment with render,
// Citro3D CPU/GPU, and memory timings. Calls compile out at their call sites
// for release builds.
void I_3DSFrameTelemetryBegin();
void I_3DSFrameTelemetryEnd();
void I_3DSFrameTelemetryDraw(unsigned int topology, unsigned int vertices, bool indexed);

// Called from the SDL event pump on the main thread. It only updates the
// combo state and latches a request; it never performs filesystem I/O.
F3DSDiagnosticButtonResult I_3DSDiagnosticButtonEvent(unsigned int button, bool pressed);

// Called from the main thread after input polling, before the next frame is
// rendered. A pending request is consumed at most once per L+R+A press.
void I_3DSServiceDiagnosticDump();

// Latches the same request as the hardware chord. This exists so automated
// emulator runs can exercise the complete dump writer even when their input
// driver cannot hold three ordinary keyboard keys simultaneously.
void I_3DSRequestDiagnosticDump();

// Writes engine-level state that is meaningful without symbolizing the raw
// process-memory capture (map, player/camera, input command and render state).
// Implemented in d_main.cpp, where the engine globals are already available.
bool I_3DSWriteEngineDiagnosticSnapshot(const char *path);
