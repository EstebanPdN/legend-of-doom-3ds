#include "i_input_3ds.h"

#include "d_eventbase.h"
#include "keydef.h"

namespace
{
constexpr float TouchWidth = 320.0f;
constexpr float TouchHeight = 240.0f;
constexpr float TouchGain = 2.0f;

float TouchRemainderX;
float TouchRemainderY;

int ConsumeWholeDelta(float &value)
{
	const int whole = static_cast<int>(value);
	value -= whole;
	return whole;
}
}

int I_3DSMapJoystickButton(unsigned int button)
{
	// SDL's N3DS joystick indices are the corresponding libctru HID bits.
	// Keep these as standard gamepad keys so menus and default bindings share
	// the same behavior as the mature XInput and raw-PS2 backends.
	static constexpr int ButtonMap[] =
	{
		KEY_PAD_A,            // 0: A
		KEY_PAD_B,            // 1: B
		KEY_PAD_BACK,         // 2: Select
		KEY_PAD_START,        // 3: Start
		KEY_PAD_DPAD_RIGHT,   // 4: D-pad right
		KEY_PAD_DPAD_LEFT,    // 5: D-pad left
		KEY_PAD_DPAD_UP,      // 6: D-pad up
		KEY_PAD_DPAD_DOWN,    // 7: D-pad down
		KEY_PAD_RSHOULDER,    // 8: R
		KEY_PAD_LSHOULDER,    // 9: L
		KEY_PAD_X,            // 10: X
		KEY_PAD_Y,            // 11: Y
		0,                    // 12: unused HID bit
		0,                    // 13: unused HID bit
		KEY_PAD_LTRIGGER,     // 14: ZL (New 3DS)
		KEY_PAD_RTRIGGER,     // 15: ZR (New 3DS)
	};

	return button < sizeof(ButtonMap) / sizeof(ButtonMap[0]) ? ButtonMap[button] : 0;
}

void I_3DSHandleTouchEvent(const SDL_Event &event, bool guiCapture)
{
	switch (event.type)
	{
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
		TouchRemainderX = 0.0f;
		TouchRemainderY = 0.0f;
		break;

	case SDL_FINGERMOTION:
		if (!guiCapture)
		{
			TouchRemainderX += event.tfinger.dx * TouchWidth * TouchGain;
			TouchRemainderY += event.tfinger.dy * TouchHeight * TouchGain;
			PostMouseMove(ConsumeWholeDelta(TouchRemainderX), ConsumeWholeDelta(TouchRemainderY));
		}
		break;

	default:
		break;
	}
}
