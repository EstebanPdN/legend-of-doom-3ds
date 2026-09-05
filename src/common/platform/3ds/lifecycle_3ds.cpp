#include "lifecycle_3ds.h"

#include <3ds.h>

#include <atomic>

namespace
{
constexpr u64 PollIntervalNanoseconds = 50ULL * 1000ULL * 1000ULL;
constexpr u64 UnresponsiveGraceMilliseconds = 8000;
constexpr size_t SupervisorStackBytes = 16 * 1024;

std::atomic<bool> StopRequested{false};
Thread SupervisorThread = nullptr;

void ExitSupervisorMain(void *)
{
	u64 requestStarted = 0;
	while (!StopRequested.load(std::memory_order_acquire))
	{
		// During a normal HOME transition libctru clears the application's active
		// state before it waits in the menu. Only treat HOME as wedged while this
		// process is still active; otherwise returning from HOME would be killed
		// by the supervisor after 1.5 seconds.
		const bool exitRequested = aptShouldClose()
			|| (aptShouldJumpToHome() && aptIsActive());
		const u64 now = osGetTime();
		if (!exitRequested)
		{
			requestStarted = 0;
		}
		else if (requestStarted == 0)
		{
			requestStarted = now;
		}
		else if (now - requestStarted >= UnresponsiveGraceMilliseconds)
		{
			// Normal code gets eight seconds to service APT and shut down. If it is
			// wedged in C3D/GSP, terminate this process without entering teardown,
			// which would wait on the same GPU queue forever.
			svcExitProcess();
		}
		svcSleepThread(PollIntervalNanoseconds);
	}
}
}

bool I_3DSStartExitSupervisor()
{
	if (SupervisorThread != nullptr) return true;
	StopRequested.store(false, std::memory_order_release);

	// Core 2 is application-accessible on the New 3DS and remains responsive if
	// core 0 is blocked. Fall back to core 0 so a restrictive 3DSX environment
	// still gets protection while the main thread sleeps in a kernel wait.
	SupervisorThread = threadCreate(ExitSupervisorMain, nullptr,
		SupervisorStackBytes, 0x2f, 2, false);
	if (SupervisorThread == nullptr)
	{
		SupervisorThread = threadCreate(ExitSupervisorMain, nullptr,
			SupervisorStackBytes, 0x2f, 0, false);
	}
	return SupervisorThread != nullptr;
}

void I_3DSStopExitSupervisor()
{
	if (SupervisorThread == nullptr) return;
	StopRequested.store(true, std::memory_order_release);
	threadJoin(SupervisorThread, U64_MAX);
	threadFree(SupervisorThread);
	SupervisorThread = nullptr;
}
