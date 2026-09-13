// Copy the engine-rendered caption, including the Options font and translation.
void DrawUpdateCaption(unsigned char *framebuffer, const unsigned char *menu,
    int pitch, int width, int height)
{
    int left = width, right = -1, top = -1, bottom = -1, empty = 0;
    for (int y = 0; y < height; ++y)
    {
        bool occupied = false;
        for (int x = 0; x < width; ++x)
        {
            const auto *pixel = menu + y * pitch + x * 4;
            if (!(pixel[0] || pixel[1] || pixel[2])) continue;
            occupied = true;
            left = std::min(left, x); right = std::max(right, x);
        }
        if (occupied) { if (top < 0) top = y; bottom = y; empty = 0; }
        else if (top >= 0 && ++empty >= 4) break;
    }
    if (top < 0 || right < left) return;
    const float scale = std::min(1.44f, 304.f / (right - left + 1));
    const int targetLeft = (320 - int(std::ceil((right - left + 1) * scale))) / 2;
    for (int y = 0; y < int(std::ceil((bottom - top + 1) * scale)); ++y)
    for (int x = 0; x < int(std::ceil((right - left + 1) * scale)); ++x)
    {
        const int sx = std::min(right, left + int(x / scale));
        const int sy = std::min(bottom, top + int(y / scale));
        const auto *pixel = menu + sy * pitch + sx * 4;
        if (pixel[0] || pixel[1] || pixel[2])
            OverlayPutPixel(framebuffer, targetLeft + x, 18 + y,
                FOverlayColor{pixel[2], pixel[1], pixel[0]});
    }
}

bool DrawUpdateMenu(unsigned char *framebuffer, const unsigned char *menu,
    int pitch, int width, int height)
{
    if (!CurrentMenu || !CurrentMenu->IsKindOf("LegendUpdateMenu")) return false;
    auto font = NewSmallFont;
    auto text = [&](int y, const char *label, float scale, FOverlayColor color)
    {
        scale = std::min(scale, 276.f / std::max(1, NativeFontTextWidth(font, label)));
        const int labelWidth = std::lround(NativeFontTextWidth(font, label) * scale);
        DrawBottomScaledFontText(framebuffer, font, (320-labelWidth)/2, y, label, scale, color);
    };
    UpdateStatus status;
    Updater_GetStatus(&status);
    DrawUpdateCaption(framebuffer, menu, pitch, width, height);
    if (CurrentMenu->IntVar(FName("NativeUpdateConfirm")))
    {
        text(86, "INSTALL AND CLOSE?", 1.28f, OverlayIvory);
        text(116, "UNSAVED PROGRESS WILL BE LOST", 1.f, OverlayIvory);
        const int choice = CurrentMenu->IntVar(FName("NativeUpdateChoice"));
        DrawBottomScaledFontText(framebuffer, font, 86, 170, "YES", 1.28f,
            choice == 0 ? OverlayRed : OverlayIvory);
        DrawBottomScaledFontText(framebuffer, font, 216, 170, "NO", 1.28f,
            choice == 1 ? OverlayRed : OverlayIvory);
        DrawBottomScaledFontText(framebuffer, font, choice == 0 ? 70 : 200, 170,
            ">", 1.28f, OverlayRed);
        return true;
    }
    text(58, "INSTALLED " LOD3DS_PORT_VERSION, 1.28f, OverlayIvory);
    char latest[72];
    std::snprintf(latest, sizeof(latest), "LATEST %s", status.version[0] ? status.version : "--");
    text(78, latest, 1.28f, OverlayIvory);
    text(105, status.message[0] ? status.message : "CHECK FOR A NEW VERSION", 1.15f, OverlayIvory);
    if (Updater_Busy())
    {
        OverlayRect(framebuffer, 28, 139, 264, 7, OverlayInk);
        OverlayRect(framebuffer, 29, 140, 262, 5, FOverlayColor{60,60,60});
        OverlayRect(framebuffer, 29, 140, 262*status.progress/100, 5, OverlayRed);
        char progress[24];
        std::snprintf(progress, sizeof(progress), "%u%%", status.progress);
        if (status.state != UPDATE_CHECKING) text(158, progress, 1.28f, OverlayIvory);
        text(188, status.state == UPDATE_INSTALLING ? "PLEASE WAIT" : "B: CANCEL", 1.28f, OverlayIvory);
        return true;
    }
    NativeMenuCustomOption = true;
    auto descriptor = CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
    const char *labels[] = { status.prerelease ? "CHANNEL: EXPERIMENTAL" : "CHANNEL: STABLE",
        "CHECK FOR UPDATES", "INSTALL UPDATE", "BACK" };
    for (int row=0; row<4; ++row)
    {
        const int y = 139+row*25;
        const bool selected = descriptor && descriptor->mSelectedItem == row;
        auto color = row == 2 && status.state != UPDATE_AVAILABLE
            ? FOverlayColor{112,112,112} : OverlayIvory;
        text(y, labels[row], 1.28f, color);
        if (selected)
        {
            const int labelWidth = std::min(276, int(std::lround(NativeFontTextWidth(font,labels[row])*1.28f)));
            DrawBottomScaledFontText(framebuffer, font, (320-labelWidth)/2-15, y, ">", 1.28f, OverlayRed);
        }
        AddNativeMenuTouchRow(y-3, y+20, row);
    }
    return true;
}
