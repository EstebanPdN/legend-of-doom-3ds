#include <SDL.h>

#include "st_start.h"
#include "engineerrors.h"

class F3DSStartupScreen final : public FStartupScreen
{
public:
	explicit F3DSStartupScreen(int maxProgress) : FStartupScreen(maxProgress) {}

	void Progress() override
	{
		if (CurPos < MaxPos) ++CurPos;
	}

	bool NetLoop(bool (*timerCallback)(void *), void *userData) override
	{
		while (!timerCallback(userData)) SDL_Delay(16);
		return true;
	}
};

FStartupScreen *StartScreen;

FStartupScreen *FStartupScreen::CreateInstance(int maxProgress)
{
	return new F3DSStartupScreen(maxProgress);
}

void ST_Endoom()
{
	throw CExitEvent(0);
}
