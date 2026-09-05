#include "cache_3ds.h"

#include <3ds.h>

#include <atomic>
#include <cstdint>

namespace
{
#ifndef LOD3DS_SAFE_SOFTWARE
// 0 = unprobed, 1 = direct SVC, 2 = gsp::Gpu compatibility fallback.
std::atomic<unsigned> CacheCleanMode{0};
#endif
}

bool I_3DSCleanDataCache(const void *address, size_t size)
{
	if (address == nullptr || size == 0) return true;
	if (size > UINT32_MAX) return false;

	const auto bytes = static_cast<u32>(size);
#ifdef LOD3DS_SAFE_SOFTWARE
	// The last physically verified CPU build (v0.6) used libctru's GSP service
	// for every CPU-written scanout range. Keep the stable profile on that exact
	// cache-maintenance contract; the direct SVC remains GPU-experimental only.
	return R_SUCCEEDED(GSPGPU_FlushDataCache(address, bytes));
#else
	const auto addr = static_cast<u32>(reinterpret_cast<uintptr_t>(address));
	const unsigned mode = CacheCleanMode.load(std::memory_order_acquire);
	if (mode != 2)
	{
		const Result result = svcStoreProcessDataCache(
			CUR_PROCESS_HANDLE, addr, bytes);
		if (R_SUCCEEDED(result))
		{
			CacheCleanMode.store(1, std::memory_order_release);
			return true;
		}
		CacheCleanMode.store(2, std::memory_order_release);
	}

	return R_SUCCEEDED(GSPGPU_FlushDataCache(address, bytes));
#endif
}
