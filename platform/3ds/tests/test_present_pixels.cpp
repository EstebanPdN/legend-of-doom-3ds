#include "common/platform/3ds/present_pixels.h"
#include <cassert>
#include <vector>

int main()
{
	constexpr size_t stride = 512, rows = 256;
	std::vector<uint32_t> upload(stride * rows, 0xdeadbeef);
	// Includes resolution decreases, source padding, and the smallest canvas.
	for (auto size : {std::pair<size_t, size_t>{400, 240}, {200, 120}, {320, 192}, {1, 1}})
	{
		const auto width = size.first, height = size.second;
		for (size_t padding : {0u, 7u})
		{
			const size_t pitch = width + padding;
			std::vector<uint32_t> source(pitch * height, 0xbadbad);
			for (size_t y = 0; y < height; ++y)
				for (size_t x = 0; x < width; ++x)
					source[y * pitch + x] = static_cast<uint32_t>(y * width + x);
			const auto bytes = lod3ds::CopyPresentPixels(upload.data(), stride,
				reinterpret_cast<const uint8_t *>(source.data()), pitch * 4, width, height);
			assert(bytes == (height + 1) * stride * 4);
			for (size_t y = 0; y <= height; ++y)
				for (size_t x = 0; x <= width; ++x)
					assert(upload[y * stride + x] == source[
						(y == height ? y - 1 : y) * pitch + (x == width ? x - 1 : x)]);
		}
	}
}
