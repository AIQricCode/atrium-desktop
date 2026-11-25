// IconImage: what the icon and picture readers hand to the painters.
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace atrium {

// Premultiplied BGRA, top-down, width * height * 4 bytes, backed by an immutable shared buffer.
struct IconImage {
    int width = 0;
    int height = 0;
    std::shared_ptr<const std::vector<uint8_t>> pixels;

    IconImage() = default;
    IconImage(int w, int h, std::vector<uint8_t> data)
        : width(w),
          height(h),
          pixels(data.empty() ? nullptr : std::make_shared<const std::vector<uint8_t>>(std::move(data)))
    {
    }

    static IconImage FromBgra(int w, int h, std::vector<uint8_t> data) { return IconImage(w, h, std::move(data)); }

    const uint8_t* Data() const { return pixels ? pixels->data() : nullptr; }
    size_t ByteSize() const { return pixels ? pixels->size() : 0; }
    const std::vector<uint8_t>& Bytes() const
    {
        static const std::vector<uint8_t> kEmpty;
        return pixels ? *pixels : kEmpty;
    }
    bool Empty() const { return !pixels || pixels->empty(); }
};

} // namespace atrium
