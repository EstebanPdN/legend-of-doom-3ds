#pragma once

#include <SDL_events.h>

// Translate SDL's raw Nintendo 3DS HID bit/button index to GZDoom's standard
// gamepad key space. Returns 0 for HID bits that are not controller buttons.
int I_3DSMapJoystickButton(unsigned int button);

// Use the bottom-screen touch panel as a relative look surface. GUI capture
// owns touch while menus or text widgets explicitly request it.
void I_3DSHandleTouchEvent(const SDL_Event &event, bool guiCapture);

// Camera preferences exposed by the native Controller options menu.
// 0 = C-Stick, 1 = Touch, 2 = Both.
bool I_3DSCStickLookEnabled();
bool I_3DSTouchLookEnabled();
float I_3DSCStickSensitivity();
bool I_3DSSprintWithXEnabled();
