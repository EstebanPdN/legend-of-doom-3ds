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
		// Suspended HOME sessions are healthy; only supervise active ones.
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
			// After eight seconds, bypass potentially wedged GPU teardown.
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

	// Prefer CPU2; retain CPU0 fallback for restrictive launchers.
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
