// Computes cropped secondary-monitor capture bounds around visible glass panels.
#pragma once

#include <windows.h>

#include <algorithm>
#include <vector>

namespace atrium {

inline RECT ComputeSecondaryCopyBounds(const std::vector<RECT>& panelRects, RECT monitorBounds, int padPx, int slackPx)
{
    bool any = false;
    LONG minL = monitorBounds.right, minT = monitorBounds.bottom, maxR = monitorBounds.left, maxB = monitorBounds.top;
    for (const RECT& p : panelRects) {
        if (p.right <= p.left || p.bottom <= p.top)
            continue;
        RECT hit{};
        if (!IntersectRect(&hit, &p, &monitorBounds))
            continue;
        if (!any) {
            minL = p.left;
            minT = p.top;
            maxR = p.right;
            maxB = p.bottom;
            any = true;
        } else {
            minL = std::min(minL, p.left);
            minT = std::min(minT, p.top);
            maxR = std::max(maxR, p.right);
            maxB = std::max(maxB, p.bottom);
        }
    }
    if (!any)
        return {};
    const LONG margin = LONG(std::max(0, padPx) + std::max(0, slackPx));
    return RECT{std::max(monitorBounds.left, minL - margin), std::max(monitorBounds.top, minT - margin), std::min(monitorBounds.right, maxR + margin),
                std::min(monitorBounds.bottom, maxB + margin)};
}

// Returns true when `copyBounds` covers every panel's required backdrop ROI (including 64 px alignment headroom).
inline bool CopyBoundsCoverPanels(RECT copyBounds, const std::vector<RECT>& panelRects, RECT monitorBounds, int padPx)
{
    if (copyBounds.right <= copyBounds.left || copyBounds.bottom <= copyBounds.top)
        return false;
    const LONG reqMargin = LONG(std::max(0, padPx) + 64);
    for (const RECT& p : panelRects) {
        if (p.right <= p.left || p.bottom <= p.top)
            continue;
        RECT hit{};
        if (!IntersectRect(&hit, &p, &monitorBounds))
            continue;
        const RECT req{std::max(monitorBounds.left, p.left - reqMargin), std::max(monitorBounds.top, p.top - reqMargin),
                       std::min(monitorBounds.right, p.right + reqMargin), std::min(monitorBounds.bottom, p.bottom + reqMargin)};
        if (req.left < copyBounds.left || req.top < copyBounds.top || req.right > copyBounds.right || req.bottom > copyBounds.bottom)
            return false;
    }
    return true;
}

} // namespace atrium
