#include <3ds.h>
#include <cstdlib>
#include <cstdint>
#include <new>

#include "memory_3ds.h"

static size_t LastFailedNewRequest;
static uintptr_t LastFailedNewCaller;

namespace
{
constexpr u32 PageMask = 0xFFF;
constexpr u32 HeapRetryStepBytes = 4 * 1024 * 1024;
constexpr u32 MinimumUsableHeapBytes = 24 * 1024 * 1024;
constexpr u32 LinearHeapCapacityBytes = 32 * 1024 * 1024;
constexpr u32 ConventionalHeapAddressCapacity = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
constexpr u32 ConventionalHeapSafetyMargin = 4 * 1024 * 1024;
constexpr u32 ConventionalHeapMaximum =
	ConventionalHeapAddressCapacity - ConventionalHeapSafetyMargin;

#if defined(LOD3DS_SAFE_SOFTWARE)
constexpr u32 MinimumLinearHeapBytes = 4 * 1024 * 1024;
#else
constexpr u32 MinimumLinearHeapBytes = 32 * 1024 * 1024;
#endif

static_assert(ConventionalHeapAddressCapacity == 96 * 1024 * 1024,
	"Unexpected libctru conventional-heap address window");
static_assert(ConventionalHeapMaximum == 92 * 1024 * 1024,
	"Unexpected guarded conventional-heap maximum");
}

#if defined(LOD3DS_SAFE_SOFTWARE)
namespace
{
constexpr size_t RendererMemoryReserveBytes = 2 * 1024 * 1024;
void *RendererMemoryReserve;
}

bool I_3DSReserveRendererMemory()
{
	if (RendererMemoryReserve == nullptr)
	{
		RendererMemoryReserve = std::malloc(RendererMemoryReserveBytes);
	}
	return RendererMemoryReserve != nullptr;
}

void I_3DSReleaseRendererMemory()
{
	std::free(RendererMemoryReserve);
	RendererMemoryReserve = nullptr;
}
#else
bool I_3DSReserveRendererMemory()
{
	return true;
}

void I_3DSReleaseRendererMemory()
{
}
#endif

static void *AllocateWithNewHandler(size_t size, uintptr_t caller)
{
	if (size == 0) size = 1;
	for (;;)
	{
		if (void *block = std::malloc(size)) return block;
		LastFailedNewRequest = size;
		LastFailedNewCaller = caller;
		auto handler = std::get_new_handler();
		if (handler == nullptr) throw std::bad_alloc();
		handler();
	}
}

void *operator new(size_t size)
{
	return AllocateWithNewHandler(size, (uintptr_t)__builtin_return_address(0));
}

void *operator new[](size_t size)
{
	return AllocateWithNewHandler(size, (uintptr_t)__builtin_return_address(0));
}

size_t I_3DSLastFailedNewRequest()
{
	return LastFailedNewRequest;
}

uintptr_t I_3DSLastFailedNewCaller()
{
	return LastFailedNewCaller;
}

extern "C"
{
	extern u32 __ctru_heap;
	extern u32 __ctru_linear_heap;
	extern char *fake_heap_start;
	extern char *fake_heap_end;

	// Expose the initial linear minimum until heap allocation completes.
	u32 __ctru_heap_size = 0;
	#ifdef LOD3DS_SAFE_SOFTWARE
	u32 __ctru_linear_heap_size = 4 * 1024 * 1024;
	#else
	u32 __ctru_linear_heap_size = 32 * 1024 * 1024;
	#endif

	// Size heaps from the granted limit; keep the conventional heap below 96 MiB.
	void __system_allocateHeaps(void)
	{
		Handle resourceLimit = 0;
		Result result = svcGetResourceLimit(&resourceLimit, CUR_PROCESS_HANDLE);
		if (R_FAILED(result))
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}

		s64 maximumCommit = 0;
		s64 currentCommit = 0;
		ResourceLimitType resourceType = RESLIMIT_COMMIT;
		result = svcGetResourceLimitLimitValues(
			&maximumCommit, resourceLimit, &resourceType, 1);
		if (R_SUCCEEDED(result))
		{
			result = svcGetResourceLimitCurrentValues(
				&currentCommit, resourceLimit, &resourceType, 1);
		}
		svcCloseHandle(resourceLimit);

		if (R_FAILED(result) || maximumCommit <= currentCommit)
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}

		const u32 remaining = static_cast<u32>(maximumCommit - currentCommit) & ~PageMask;
		if (remaining <= MinimumLinearHeapBytes + MinimumUsableHeapBytes)
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}

		u32 heapBytes = remaining - MinimumLinearHeapBytes;
		if (heapBytes > ConventionalHeapMaximum)
		{
			heapBytes = ConventionalHeapMaximum;
		}
		heapBytes &= ~PageMask;

		// Keep a 4 MiB guard and retry smaller heaps when needed.
		for (;;)
		{
			__ctru_heap = 0;
			result = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0,
				heapBytes, MEMOP_ALLOC, static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE));
			if (R_SUCCEEDED(result)) break;
			if (heapBytes < MinimumUsableHeapBytes + HeapRetryStepBytes)
			{
				svcBreak(USERBREAK_PANIC);
				return;
			}
			heapBytes -= HeapRetryStepBytes;
		}
		__ctru_heap_size = heapBytes;

		// Bound remaining linear memory to the libctru 32 MiB limit.
		u32 linearBytes = remaining - heapBytes;
		if (linearBytes > LinearHeapCapacityBytes)
		{
			linearBytes = LinearHeapCapacityBytes;
		}
		linearBytes &= ~PageMask;
		if (linearBytes < MinimumLinearHeapBytes)
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}
		// Contiguous linear memory can be scarcer than the commit counter implies.
		// If the optional remainder is unavailable, retry down to the measured
		// minimum instead of turning that nonessential headroom into another
		// pre-main panic.
		for (;;)
		{
			__ctru_linear_heap = 0;
			result = svcControlMemory(&__ctru_linear_heap, 0, 0,
				linearBytes, MEMOP_ALLOC_LINEAR,
				static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE));
			if (R_SUCCEEDED(result)) break;
			if (linearBytes == MinimumLinearHeapBytes)
			{
				svcBreak(USERBREAK_PANIC);
				return;
			}
			linearBytes = linearBytes > MinimumLinearHeapBytes + HeapRetryStepBytes
				? linearBytes - HeapRetryStepBytes : MinimumLinearHeapBytes;
		}
		__ctru_linear_heap_size = linearBytes;

		mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
		fake_heap_start = reinterpret_cast<char *>(__ctru_heap);
		fake_heap_end = fake_heap_start + __ctru_heap_size;
	}
}
