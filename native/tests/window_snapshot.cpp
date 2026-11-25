// Verifies BuildWindowPictureFromBgra and Smaller match the legacy Shrink+Finish algorithm byte-for-byte.
#include "ui/window_snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using atrium::BuildWindowPictureFromBgra;
using atrium::IconImage;
using atrium::kPicturePad;
using atrium::Smaller;
using atrium::SoftShadow;
using atrium::WindowPicture;

float LegacyRoundedCoverage(int x, int y, int w, int h, float r)
{
    float px = float(x) + 0.5f, py = float(y) + 0.5f;
    float cx = std::clamp(px, r, float(w) - r), cy = std::clamp(py, r, float(h) - r);
    float d = std::hypot(px - cx, py - cy);
    return std::clamp(r + 0.5f - d, 0.f, 1.f);
}

std::vector<uint8_t> LegacyShrink(const uint8_t* src, int sw, int sh, int stride, int tw, int th)
{
    std::vector<uint32_t> sum(size_t(tw) * th * 3, 0), count(size_t(tw) * th, 0);
    std::vector<int> col(static_cast<size_t>(sw));
    for (int x = 0; x < sw; ++x)
        col[size_t(x)] = std::min(tw - 1, int(int64_t(x) * tw / sw));
    for (int y = 0; y < sh; ++y) {
        const int ty = std::min(th - 1, int(int64_t(y) * th / sh));
        const uint8_t* row = src + size_t(y) * stride;
        uint32_t* s = sum.data() + size_t(ty) * tw * 3;
        uint32_t* n = count.data() + size_t(ty) * tw;
        for (int x = 0; x < sw; ++x) {
            const int tx = col[size_t(x)];
            s[tx * 3] += row[x * 4];
            s[tx * 3 + 1] += row[x * 4 + 1];
            s[tx * 3 + 2] += row[x * 4 + 2];
            ++n[tx];
        }
    }
    std::vector<uint8_t> out(size_t(tw) * th * 4);
    for (size_t i = 0; i < size_t(tw) * th; ++i) {
        const uint32_t n = std::max<uint32_t>(1, count[i]);
        out[i * 4] = uint8_t(sum[i * 3] / n);
        out[i * 4 + 1] = uint8_t(sum[i * 3 + 1] / n);
        out[i * 4 + 2] = uint8_t(sum[i * 3 + 2] / n);
        out[i * 4 + 3] = 255;
    }
    return out;
}

WindowPicture LegacyFinish(std::vector<uint8_t> pixels, int w, int h, float radius, bool shadow)
{
    WindowPicture p;
    p.width = w;
    p.height = h;
    const int outW = w + 2 * kPicturePad, outH = h + 2 * kPicturePad;
    std::vector<uint8_t> bgra(size_t(outW) * outH * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float a = LegacyRoundedCoverage(x, y, w, h, radius) * (float(pixels[(size_t(y) * w + x) * 4 + 3]) / 255.f);
            const uint8_t* s = &pixels[(size_t(y) * w + x) * 4];
            uint8_t* d = &bgra[(size_t(y + kPicturePad) * outW + x + kPicturePad) * 4];
            d[0] = uint8_t(std::lround(s[0] * a));
            d[1] = uint8_t(std::lround(s[1] * a));
            d[2] = uint8_t(std::lround(s[2] * a));
            d[3] = uint8_t(std::lround(255 * a));
        }
    p.image = IconImage::FromBgra(outW, outH, std::move(bgra));
    if (shadow)
        p.shadow = SoftShadow(w, h);
    p.ok = true;
    return p;
}

WindowPicture LegacyReference(const uint8_t* src, int sw, int sh, int stride, int tw, int th, float radius, bool shadow)
{
    return LegacyFinish(LegacyShrink(src, sw, sh, stride, tw, th), tw, th, radius, shadow);
}

std::vector<uint8_t> MakePatternBgra(int w, int h, int stride)
{
    // Fill stride padding with distinct non-zero values to catch any out-of-bounds reads.
    std::vector<uint8_t> buf(size_t(h) * stride, 0xA5);
    static constexpr uint8_t kAlphas[3] = {0, 128, 255};
    for (int y = 0; y < h; ++y) {
        uint8_t* row = buf.data() + size_t(y) * stride;
        for (int x = 0; x < w; ++x) {
            row[x * 4 + 0] = uint8_t((x * 13 + y * 7) & 0xFF);
            row[x * 4 + 1] = uint8_t((x * 5 + y * 19 + 41) & 0xFF);
            row[x * 4 + 2] = uint8_t((x * 29 + y * 3 + 97) & 0xFF);
            row[x * 4 + 3] = kAlphas[(x + y) % 3];
        }
    }
    return buf;
}

bool Check(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        return false;
    }
    return true;
}

bool CheckMatch(const WindowPicture& actual, const WindowPicture& ref, const char* label)
{
    if (!Check(actual.ok && ref.ok, label))
        return false;
    if (!Check(actual.width == ref.width && actual.height == ref.height, label))
        return false;
    if (!Check(actual.image.width == ref.image.width && actual.image.height == ref.image.height, label))
        return false;
    if (!Check(actual.image.Bytes() == ref.image.Bytes(), label))
        return false;
    if (!Check(actual.shadow.width == ref.shadow.width && actual.shadow.height == ref.shadow.height, label))
        return false;
    if (!Check(actual.shadow.Bytes() == ref.shadow.Bytes(), label))
        return false;
    return true;
}

} // namespace

int main()
{
    // 1. Same-size fast path with non-tight stride and varying source alpha (0, 128, 255).
    {
        constexpr int sw = 64, sh = 48, stride = (64 + 7) * 4;
        const auto src = MakePatternBgra(sw, sh, stride);
        for (bool shadow : {true, false}) {
            const WindowPicture actual = BuildWindowPictureFromBgra(src.data(), sw, sh, stride, sw, sh, 8.f, shadow);
            const WindowPicture ref = LegacyReference(src.data(), sw, sh, stride, sw, sh, 8.f, shadow);
            if (!CheckMatch(actual, ref, shadow ? "same-size non-tight stride shadow=true" : "same-size non-tight stride shadow=false"))
                return 1;
        }
    }

    // 2. Integer downscale (128x96 -> 32x24, radius=6.f).
    {
        constexpr int sw = 128, sh = 96, stride = sw * 4, tw = 32, th = 24;
        const auto src = MakePatternBgra(sw, sh, stride);
        const WindowPicture actual = BuildWindowPictureFromBgra(src.data(), sw, sh, stride, tw, th, 6.f, true);
        const WindowPicture ref = LegacyReference(src.data(), sw, sh, stride, tw, th, 6.f, true);
        if (!CheckMatch(actual, ref, "integer downscale 128x96 -> 32x24"))
            return 1;
    }

    // 3. Non-integer downscale with non-tight stride (137x93, stride=145*4 -> 53x41, radius=7.5f).
    {
        constexpr int sw = 137, sh = 93, stride = 145 * 4, tw = 53, th = 41;
        const auto src = MakePatternBgra(sw, sh, stride);
        const WindowPicture actual = BuildWindowPictureFromBgra(src.data(), sw, sh, stride, tw, th, 7.5f, true);
        const WindowPicture ref = LegacyReference(src.data(), sw, sh, stride, tw, th, 7.5f, true);
        if (!CheckMatch(actual, ref, "non-integer downscale 137x93 -> 53x41"))
            return 1;
    }

    // 4. Smaller() round-trip on a padded WindowPicture (200x120 -> largeMax=100).
    {
        constexpr int sw = 200, sh = 120, stride = sw * 4;
        const auto src = MakePatternBgra(sw, sh, stride);
        WindowPicture large = BuildWindowPictureFromBgra(src.data(), sw, sh, stride, sw, sh, 12.f, false);
        large.corner = 12.f;

        const WindowPicture smaller = Smaller(large, 100);
        if (!Check(smaller.ok && smaller.width == 100 && smaller.height == 60, "Smaller() dimensions mismatch"))
            return 1;
        if (!Check(std::fabs(smaller.corner - 6.f) < 1e-5f, "Smaller() corner scale mismatch"))
            return 1;
        if (!Check(smaller.shadow.Empty(), "Smaller() must not attach a shadow"))
            return 1;

        const int padStride = large.image.width * 4;
        const uint8_t* paddedSrc = large.image.Data() + size_t(kPicturePad) * padStride + size_t(kPicturePad) * 4;
        const WindowPicture ref = LegacyReference(paddedSrc, large.width, large.height, padStride, 100, 60, 6.f, false);
        if (!CheckMatch(smaller, ref, "Smaller() padded round-trip byte equivalence"))
            return 1;
    }

    // 5. Invalid inputs return an empty/not-ok picture safely.
    {
        const uint8_t px[16] = {};
        if (!Check(!BuildWindowPictureFromBgra(nullptr, 4, 4, 16, 4, 4, 0.f).ok, "null src should fail"))
            return 1;
        if (!Check(!BuildWindowPictureFromBgra(px, 0, 4, 16, 4, 4, 0.f).ok, "zero sw should fail"))
            return 1;
        if (!Check(!BuildWindowPictureFromBgra(px, 4, 4, 12, 4, 4, 0.f).ok, "undersized stride should fail"))
            return 1;
        if (!Check(!BuildWindowPictureFromBgra(px, 4, 4, 16, 0, 4, 0.f).ok, "zero tw should fail"))
            return 1;
    }

    return 0;
}
