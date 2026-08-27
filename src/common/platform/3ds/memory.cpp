#include <3ds/types.h>
#include <cstdlib>
#include <cstdint>
#include <new>

static size_t LastFailedNewRequest;
static uintptr_t LastFailedNewCaller;

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
	// Native/CIA contract: the New 3DS ExHeader requests 124 MiB application
	// mode. Let libctru assign every page not used by the static image/linear
	// arena to malloc/new, and reserve 32 MiB for Citro3D/NovaGL VBOs, texture
	// uploads and command staging. Four MiB was only enough to initialize the
	// bridge; it left no useful budget for GZDoom's world buffers or textures.
	// The Homebrew Launcher instead uses the explicit split in 3dsx_crt0.s.
	u32 __ctru_heap_size = 0;
	u32 __ctru_linear_heap_size = 32 * 1024 * 1024;
}
