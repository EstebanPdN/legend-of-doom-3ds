#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lod3ds
{
// The caller provides a texture larger than the canvas on both axes.
inline size_t CopyPresentPixels(uint32_t *upload, size_t textureWidth,
	const uint8_t *pixels, size_t pitch, size_t width, size_t height)
{
	for (size_t y = 0; y < height; ++y)
	{
		uint32_t *row = upload + y * textureWidth;
		std::memcpy(row, pixels + y * pitch, width * sizeof(uint32_t));
		// Bilinear samples at the canvas edge must not read stale padding.
		row[width] = row[width - 1];
	}
	std::memcpy(upload + height * textureWidth,
		upload + (height - 1) * textureWidth, (width + 1) * sizeof(uint32_t));
	return (height + 1) * textureWidth * sizeof(uint32_t);
}
}
