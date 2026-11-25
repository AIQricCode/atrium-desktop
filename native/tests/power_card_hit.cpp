#include "ui/start_menu_power_geom.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace atrium;

namespace {

constexpr float kEps = 1e-4f;
constexpr float kPowerW = 176.f, kPowerPad = 12.f, kPowerRowH = 44.f, kRowInset = 8.f;
constexpr int kRowCount = 5; // Sleep, Hibernate, Restart, Shut down, Lock
int g_failures = 0;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool Near(float a, float b) { return std::fabs(a - b) <= kEps; }

bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }

D2D1_RECT_F PowerCard()
{
    constexpr float kRight = 576.f, kBottom = 544.f;
    return D2D1::RectF(kRight - kPowerW, kBottom - (float(kRowCount) * kPowerRowH + 2.f * kPowerPad), kRight, kBottom);
}

D2D1_RECT_F RawRow(const D2D1_RECT_F& card, int index)
{
    return D2D1::RectF(card.left + kRowInset, card.top + kPowerPad + kPowerRowH * float(index), card.right - kRowInset,
                       card.top + kPowerPad + kPowerRowH * float(index + 1));
}

void CheckAnchorInvariant()
{
    const D2D1_RECT_F card = PowerCard();
    for (float k : {0.5f, 0.75f, 1.0f, 1.03f}) {
        const D2D1_RECT_F scaled = ScaleRectAbout(card, card.right, card.bottom, k);
        Require(Near(scaled.right, card.right) && Near(scaled.bottom, card.bottom), "bottom-right anchor of the power card stays fixed across all scales");
        Require(Near(scaled.left, card.right - (card.right - card.left) * k) && Near(scaled.top, card.bottom - (card.bottom - card.top) * k),
                "card top-left scales proportionally from the bottom-right anchor");
        const D2D1_RECT_F point = ScaleRectAbout(D2D1::RectF(card.right, card.bottom, card.right, card.bottom), card.right, card.bottom, k);
        Require(Near(point.left, card.right) && Near(point.top, card.bottom) && Near(point.right, card.right) && Near(point.bottom, card.bottom),
                "a degenerate rect at the anchor point stays fixed");
    }
}

void CheckAllRowsAtScales()
{
    const D2D1_RECT_F card = PowerCard();
    const float rawRowW = (card.right - kRowInset) - (card.left + kRowInset);

    for (float k : {0.5f, 0.75f, 1.0f, 1.03f}) {
        const D2D1_RECT_F scaledCard = ScaleRectAbout(card, card.right, card.bottom, k);
        std::vector<D2D1_RECT_F> hits;
        hits.reserve(kRowCount);

        for (int i = 0; i < kRowCount; ++i) {
            const D2D1_RECT_F row = RawRow(card, i);
            const D2D1_RECT_F hit = ScaleRectAbout(row, card.right, card.bottom, k);

            Require(Near(hit.left, card.right + (row.left - card.right) * k) && Near(hit.top, card.bottom + (row.top - card.bottom) * k) &&
                        Near(hit.right, card.right + (row.right - card.right) * k) && Near(hit.bottom, card.bottom + (row.bottom - card.bottom) * k),
                    "hit rect matches transformed visual box");
            Require(Near(hit.right - hit.left, rawRowW * k) && Near(hit.bottom - hit.top, kPowerRowH * k), "scaled row width and height scale by k");
            Require(hit.left > scaledCard.left && hit.right < scaledCard.right && hit.top > scaledCard.top && hit.bottom < scaledCard.bottom,
                    "scaled row remains inside the scaled power card");
            if (k == 1.0f) {
                Require(Near(hit.left, row.left) && Near(hit.top, row.top) && Near(hit.right, row.right) && Near(hit.bottom, row.bottom),
                        "at k=1.0 hit rect equals raw row rect");
            }
            hits.push_back(hit);
        }

        for (size_t i = 0; i + 1 < kRowCount; ++i) {
            Require(hits[i].bottom <= hits[i + 1].top + kEps && Near(hits[i].bottom, hits[i + 1].top), "neighboring scaled rows meet without overlapping");
        }
    }
}

void CheckMidOpenRestartAndShutdown()
{
    const D2D1_RECT_F card = PowerCard();
    constexpr int kRestart = 2;  // Sleep=0, Hibernate=1, Restart=2, Shut down=3, Lock=4
    constexpr int kShutdown = 3;

    for (float k : {0.5f, 0.75f}) {
        std::vector<D2D1_RECT_F> hits;
        hits.reserve(kRowCount);
        for (int i = 0; i < kRowCount; ++i)
            hits.push_back(ScaleRectAbout(RawRow(card, i), card.right, card.bottom, k));

        for (int target : {kRestart, kShutdown}) {
            const D2D1_RECT_F raw = RawRow(card, target);
            const float visualCenterX = card.right + ((raw.left + raw.right) * 0.5f - card.right) * k;
            const float visualCenterY = card.bottom + ((raw.top + raw.bottom) * 0.5f - card.bottom) * k;

            Require(Contains(hits[size_t(target)], visualCenterX, visualCenterY),
                    "visual center of Restart/Shut down mid-open lands inside its scaled hit rect");
            for (int other = 0; other < kRowCount; ++other) {
                if (other != target) {
                    Require(!Contains(hits[size_t(other)], visualCenterX, visualCenterY),
                            "visual center of Restart/Shut down does not overlap neighboring rows");
                }
            }
            // At k=0.5f, the visual center of Restart/Shut down has shifted below its unscaled row bounds.
            if (k == 0.5f)
                Require(!Contains(raw, visualCenterX, visualCenterY), "at k=0.5f unscaled row rect would have missed the transformed visual row center");
        }
    }
}

} // namespace

int main()
{
    CheckAnchorInvariant();
    CheckAllRowsAtScales();
    CheckMidOpenRestartAndShutdown();
    if (!g_failures)
        std::cout << "power_card_hit: all checks passed\n";
    return g_failures ? 1 : 0;
}
