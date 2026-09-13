// Match the Controls font's shaded pixels, including its dark outline. Painting
// every opaque pixel white fills the outline and closes the counters in letters.
void DrawUpdateTopText(unsigned char *pixels, int pitch, int width, int height,
    FFont *font, int x, int y, const char *text, FOverlayColor color)
{
    constexpr float scale = 1.28f;
    float pen = 0;
    for (const unsigned char *c = reinterpret_cast<const unsigned char *>(text); *c; ++c)
    {
        int advance = font->GetSpaceWidth();
        auto *glyph = font->GetChar(*c, CR_UNTRANSLATED, &advance);
        if (glyph && glyph->GetTexture())
        {
            FBitmap bitmap = glyph->GetTexture()->GetBgraBitmap(nullptr);
            for (int py = 0; bitmap.GetPixels() && py < bitmap.GetHeight(); ++py)
            for (int px = 0; px < bitmap.GetWidth(); ++px)
            {
                const auto *pixel = bitmap.GetPixels() + py * bitmap.GetPitch() + px * 4;
                if (pixel[3] < 32) continue;
                const unsigned shade = std::max({pixel[0], pixel[1], pixel[2]});
                const FOverlayColor ink{
                    static_cast<unsigned char>(color.Red * shade / 255u),
                    static_cast<unsigned char>(color.Green * shade / 255u),
                    static_cast<unsigned char>(color.Blue * shade / 255u)};
                const int left = std::lround((pen + px) * scale);
                const int top = std::lround(py * scale);
                NativeTopRect(pixels, pitch, width, height, x + left, y + top,
                    std::lround((pen + px + 1) * scale) - left,
                    std::lround((py + 1) * scale) - top, ink, 255);
            }
        }
        pen += advance + font->GetDefaultKerning();
    }
}

void DrawUpdateTopBackground(unsigned char *pixels, int pitch, int width, int height)
{
    // Select the title artwork explicitly, even when the idle sequence is on
    // ZSTORY or the update menu was opened from a paused game.
    const auto id = TexMan.CheckForTexture("TITLEPIC", ETextureType::Any,
        FTextureManager::TEXMAN_TryAny);
    auto *art = BottomMapTexture(id);
    const unsigned brightness = NativeMenuCurrentBrightness();
    for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
        auto *target = pixels + y * pitch + x * 4;
        const auto *source = art ? art->Pixels.data() +
            (y * art->Height / height) * art->Pitch + (x * art->Width / width) * 4 : nullptr;
        for (int channel = 0; channel < 3; ++channel)
            target[channel] = source ? NativeMenuDimChannel(source[channel], brightness) : 0;
        target[3] = 255;
    }
}

bool DrawUpdateChangelogTop(unsigned char *pixels, int pitch, int width, int height)
{
    if (!CurrentMenu || !CurrentMenu->IsKindOf("LegendUpdateMenu")) return false;
    auto font = NewSmallFont;
    if (!font) return false;
    static char bundled[12289] = {}, cached[12289] = {}, lines[768][43];
    static bool loaded = false;
    static unsigned count = 0;
    static int cachedWidth = 0;
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
    if (!count || std::strcmp(body, cached) || cachedWidth != width)
    {
        std::snprintf(cached, sizeof(cached), "%s", body);
        cachedWidth = width;
        static char formatted[384][43];
        const unsigned formattedCount = Update_FormatNotes(cached, formatted, 384);
        count = 0;
        // Measure the proportional font at its actual drawing scale. A fixed
        // character count alone can push wide glyphs beyond the LCD edge.
        for (unsigned row = 0; row < formattedCount && count < 768; ++row)
        {
            const char *remaining = formatted[row];
            do
            {
                size_t take = std::strlen(remaining);
                char candidate[43];
                std::strcpy(candidate, remaining);
                while (take > 1 && std::lround(NativeFontTextWidth(font, candidate)*1.28f) > width-32)
                    candidate[--take] = 0;
                if (take < std::strlen(remaining))
                {
                    size_t word = take;
                    while (word && remaining[word] != ' ') --word;
                    if (word) take = word;
                }
                std::memcpy(lines[count], remaining, take);
                lines[count++][take] = 0;
                remaining += take;
                while (*remaining == ' ') ++remaining;
            } while (*remaining && count < 768);
        }
        CurrentMenu->IntVar(FName("NativeUpdatePage")) = 0;
    }
    unsigned first = 0;
    if (count && !std::strcmp(lines[0], "Changelog"))
        while (++first < count && !lines[first][0]) {}
    const unsigned rowHeight = std::lround(font->GetHeight()*1.28f) + 5;
    const unsigned rows = std::max(1, (height - 78) / int(rowHeight));
    const unsigned pages = std::max(1u, (count-first+rows-1)/rows);
    int &page = CurrentMenu->IntVar(FName("NativeUpdatePage"));
    page = std::clamp(page, 0, int(pages)-1);
    CurrentMenu->IntVar(FName("NativeUpdatePages")) = pages;
    DrawUpdateTopBackground(pixels, pitch, width, height);
    auto centered = [&](int y, const char *text, FOverlayColor color)
    {
        DrawUpdateTopText(pixels, pitch, width, height, font,
            (width-std::lround(NativeFontTextWidth(font, text)*1.28f))/2, y, text, color);
    };
    centered(10, "CHANGELOG", OverlayRed);
    UpdateStatus status;
    Updater_GetStatus(&status);
    const char *version = onlineNotes && status.version[0] ? status.version : LOD3DS_PORT_VERSION;
    centered(29, version, OverlayIvory);
    for (unsigned i = first + page*rows; i < count && i < first+(page+1)*rows; ++i)
        DrawUpdateTopText(pixels, pitch, width, height, font, 16,
            52+(i-first-page*rows)*rowHeight, lines[i], OverlayIvory);
    char footer[64];
    std::snprintf(footer, sizeof(footer), "L / R: PAGE %d / %u", page+1, pages);
    centered(height-20, footer, OverlayIvory);
    return true;
}
