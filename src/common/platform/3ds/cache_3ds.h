#pragma once

#include <cstddef>

// Makes CPU writes visible to GPU/DSP consumers. CPU-to-device transfers need
// a clean (store), not a clean+invalidate. The direct SVC avoids a synchronous
// gsp::Gpu round trip; launchers that reject it retain the service fallback.
bool I_3DSCleanDataCache(const void *address, size_t size);
