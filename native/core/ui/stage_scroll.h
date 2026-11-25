// Stage strip paging (cyclic by default, or clamped at ends), including precision wheels and horizontal touchpad input.
#pragma once

#include <algorithm>
#include <cstddef>

namespace atrium {

class StageScroll {
public:
    size_t Offset() const { return offset_; }
    bool Wrap() const { return wrap_; }
    void SetWrap(bool wrap) { wrap_ = wrap; }
    void Reset()
    {
        offset_ = 0;
        vertical_ = horizontal_ = 0;
    }
    void Fit(size_t count, size_t capacity = 1)
    {
        if (!count)
            Reset();
        else if (wrap_)
            offset_ %= count;
        else
            offset_ = std::min(offset_, MaxOffset(count, capacity));
    }
    void Anchor(size_t offset, size_t count, size_t capacity = 1)
    {
        if (!count)
            offset_ = 0;
        else if (wrap_)
            offset_ = offset % count;
        else
            offset_ = std::min(offset, MaxOffset(count, capacity));
    }
    size_t Index(size_t slot, size_t count) const { return count ? (offset_ + slot) % count : 0; }

    bool Wheel(int delta, bool horizontal, size_t count, size_t capacity = 1)
    {
        const size_t maxOffset = MaxOffset(count, capacity);
        if (count < 2 || (!wrap_ && maxOffset == 0)) {
            Reset();
            return false;
        }
        int& remainder = horizontal ? horizontal_ : vertical_;
        remainder += delta;
        const int steps = remainder / 120;
        remainder %= 120;
        const auto old = offset_;
        const auto move = horizontal ? steps : -steps;
        if (wrap_) {
            const auto n = static_cast<long long>(count);
            offset_ = size_t((static_cast<long long>(offset_ % count) + move % n + n) % n);
        } else {
            const size_t clamped = size_t(std::clamp<long long>(static_cast<long long>(offset_) + move, 0, static_cast<long long>(maxOffset)));
            // Discard fractional overflow pushed into a clamped end so reversing responds on the next notch.
            if ((clamped == 0 && (horizontal ? remainder < 0 : remainder > 0)) || (clamped == maxOffset && (horizontal ? remainder > 0 : remainder < 0)))
                remainder = 0;
            offset_ = clamped;
        }
        return old != offset_;
    }

private:
    static size_t MaxOffset(size_t count, size_t capacity) { return count > capacity ? count - capacity : 0; }

    size_t offset_ = 0;
    int vertical_ = 0, horizontal_ = 0;
    bool wrap_ = true;
};

} // namespace atrium
