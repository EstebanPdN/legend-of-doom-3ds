bool DrawUpdateChangelogTop(unsigned char *pixels, int pitch, int width, int height)
{
    if (!CurrentMenu || !CurrentMenu->IsKindOf("LegendUpdateMenu")) return false;
    auto font = NewSmallFont;
    if (!font) return false;
    static char bundled[12289] = {}, cached[12289] = {}, lines[384][43];
    static bool loaded = false;
    static unsigned count = 0;
    if (!loaded)
    {
        loaded = true;
        if (FILE *file = std::fopen("romfs:/update-changelog.txt", "rb"))
        {
            bundled[std::fread(bundled, 1, sizeof(bundled)-1, file)] = 0;
            std::fclose(file);
        }
    }
    char notes[12289] = {};
    Updater_GetNotes(notes, sizeof(notes));
    const bool onlineNotes = notes[0] != 0;
    const char *body = onlineNotes ? notes : bundled;
    if (!count || std::strcmp(body, cached))
    {
        std::snprintf(cached, sizeof(cached), "%s", body);
        count = Update_FormatNotes(cached, lines, 384);
        CurrentMenu->IntVar(FName("NativeUpdatePage")) = 0;
    }
    unsigned first = 0;
    if (count && !std::strcmp(lines[0], "Changelog"))
        while (++first < count && !lines[first][0]) {}
    const unsigned rowHeight = font->GetHeight() + 4;
    const unsigned rows = std::max(1, (height - 78) / int(rowHeight));
    const unsigned pages = std::max(1u, (count-first+rows-1)/rows);
    int &page = CurrentMenu->IntVar(FName("NativeUpdatePage"));
    page = std::clamp(page, 0, int(pages)-1);
    CurrentMenu->IntVar(FName("NativeUpdatePages")) = pages;
    NativeTopRect(pixels, pitch, width, height, 0, 0, width, height, OverlayInk, 255);
    NativeTopCenteredFontText(pixels, pitch, width, height, font, 0, width, 12,
        "CHANGELOG", 1, OverlayRed);
    UpdateStatus status;
    Updater_GetStatus(&status);
    const char *version = onlineNotes && status.version[0] ? status.version : LOD3DS_PORT_VERSION;
    NativeTopCenteredFontText(pixels, pitch, width, height, font, 0, width, 28,
        version, 1, OverlayIvory);
    for (unsigned i = first + page*rows; i < count && i < first+(page+1)*rows; ++i)
        NativeTopFontText(pixels, pitch, width, height, font, 16,
            48+(i-first-page*rows)*rowHeight, lines[i], 1, OverlayIvory);
    char footer[64];
    std::snprintf(footer, sizeof(footer), "L / R: PAGE %d / %u", page+1, pages);
    NativeTopCenteredFontText(pixels, pitch, width, height, font, 0, width,
        height-20, footer, 1, OverlayIvory);
    return true;
}
