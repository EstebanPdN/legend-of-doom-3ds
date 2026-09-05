#pragma once

#include <cstddef>
#include <cstdint>

// Hold one contiguous block while GZDoom parses ZScript, then return it just
// before SoftPoly creates its large vertex buffer. This prevents abundant but
// fragmented free heap from making that allocation fail on physical hardware.
bool I_3DSReserveRendererMemory();
void I_3DSReleaseRendererMemory();

size_t I_3DSLastFailedNewRequest();
uintptr_t I_3DSLastFailedNewCaller();
