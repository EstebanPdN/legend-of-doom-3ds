#pragma once

// The libctru APT event thread keeps receiving HOME/close requests even if the
// main game thread is blocked in a GPU wait. This supervisor gives a responsive
// game time to perform normal cleanup, then terminates only the current process
// instead of forcing the user to power-cycle the console.
bool I_3DSStartExitSupervisor();
void I_3DSStopExitSupervisor();
