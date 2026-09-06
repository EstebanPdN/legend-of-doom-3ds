#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

struct PcmWave
{
    const uint8_t *samples = nullptr;
    size_t bytes = 0;
    int rate = 0, channels = 0, bits = 0;
};

inline bool ReadPcmWave(const uint8_t *data, size_t size, PcmWave &wave)
{
    auto u16 = [](const uint8_t *p) { return unsigned(p[0]) | unsigned(p[1]) << 8; };
    auto u32 = [](const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
        uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    wave = {};
    if (size < 12 || std::memcmp(data, "RIFF", 4) || std::memcmp(data + 8, "WAVE", 4)) return false;
    const size_t riff = u32(data + 4);
    if (riff < 4 || riff > size - 8) return false;
    const size_t end = riff + 8;
    bool format = false;
    unsigned align = 0;
    for (size_t pos = 12; pos + 8 <= end;)
    {
        const size_t length = u32(data + pos + 4);
        const uint8_t *chunk = data + pos + 8;
        if (length > end - pos - 8) return false;
        if (!std::memcmp(data + pos, "fmt ", 4))
        {
            if (length < 16 || u16(chunk) != 1) return false;
            wave.channels = u16(chunk + 2);
            const uint32_t rate = u32(chunk + 4);
            wave.bits = u16(chunk + 14);
            align = u16(chunk + 12);
            if ((wave.channels != 1 && wave.channels != 2) ||
                (wave.bits != 8 && wave.bits != 16) || rate == 0 || rate > 192000 ||
                align != unsigned(wave.channels * wave.bits / 8)) return false;
            wave.rate = rate;
            format = true;
        }
        else if (!std::memcmp(data + pos, "data", 4))
        {
            wave.samples = chunk;
            wave.bytes = length;
        }
        pos += 8 + length;
        if (length & 1) ++pos;
    }
    return format && wave.samples && wave.bytes > 0 && wave.bytes % align == 0;
}
