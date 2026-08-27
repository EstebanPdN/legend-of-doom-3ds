#include "cmdlib.h"
#include "i_system.h"

namespace
{
const char *const DataRoot = "sdmc:/3ds/legend-of-doom/";

FString EnsureDirectory(const char *suffix)
{
	FString path(DataRoot);
	path += suffix;
	CreatePath(path);
	return path;
}
}

FString GetUserFile(const char *file)
{
	FString path = EnsureDirectory("");
	path += file;
	return path;
}

FString M_GetAppDataPath(bool create)
{
	return create ? EnsureDirectory("") : FString(DataRoot);
}

FString M_GetCachePath(bool create)
{
	return create ? EnsureDirectory("cache/") : FString(DataRoot) + "cache/";
}

FString M_GetAutoexecPath() { return GetUserFile("autoexec.cfg"); }
FString M_GetConfigPath(bool) { return GetUserFile("legend-of-doom.ini"); }
FString M_GetScreenshotsPath() { return EnsureDirectory("screenshots/"); }
FString M_GetSavegamesPath() { return EnsureDirectory("saves/"); }
FString M_GetDocumentsPath() { return EnsureDirectory(""); }
FString M_GetDemoPath() { return EnsureDirectory("demos/"); }
FString M_GetNormalizedPath(const char *path) { return FString(path); }
