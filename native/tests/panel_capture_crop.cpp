#include "render/panel_capture_bounds.h"

#include <cstdio>

namespace {

using atrium::ComputeSecondaryCopyBounds;
using atrium::CopyBoundsCoverPanels;

bool RectEq(RECT a, RECT b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

bool Check(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const RECT mon{1920, 0, 3840, 1080};
    constexpr int kPad = 24;
    constexpr int kSlack = 128;

    // 1. Single panel adds pad + slack clamped inside monitor.
    const RECT single{2200, 300, 2600, 700};
    const RECT b1 = ComputeSecondaryCopyBounds({single}, mon, kPad, kSlack);
    if (!Check(RectEq(b1, RECT{2048, 148, 2752, 852}), "single panel bounds mismatch"))
        return 1;
    if (!Check(CopyBoundsCoverPanels(b1, {single}, mon, kPad), "initial crop should cover panel"))
        return 1;

    // 2. Small move within 64 px fits inside 128 px slack (needs >= pad + 64 alignment margin).
    const RECT shifted{2240, 340, 2640, 740};
    if (!Check(CopyBoundsCoverPanels(b1, {shifted}, mon, kPad), "shifted panel within slack should stay covered"))
        return 1;

    // 3. Large move beyond slack requires re-cropping.
    const RECT movedFar{2300, 300, 2700, 700};
    if (!Check(!CopyBoundsCoverPanels(b1, {movedFar}, mon, kPad), "panel moved past headroom must trigger recrop"))
        return 1;

    // 4. Multi-panel union + monitor edge clamping.
    const RECT nearEdge{1930, 10, 2100, 120};
    const RECT lowerRight{3700, 900, 3830, 1070};
    const RECT bMulti = ComputeSecondaryCopyBounds({nearEdge, lowerRight}, mon, kPad, kSlack);
    if (!Check(RectEq(bMulti, mon), "multi-panel bounds should clamp cleanly to monitor"))
        return 1;
    if (!Check(CopyBoundsCoverPanels(bMulti, {nearEdge, lowerRight}, mon, kPad), "monitor-clamped crop covers edge panels"))
        return 1;

    // 5. Off-monitor or empty panels yield an empty copy rect and skip false coverage mismatches.
    const RECT primaryOnly{100, 100, 500, 500};
    const RECT emptyPanel{2100, 200, 2100, 200};
    const RECT bOff = ComputeSecondaryCopyBounds({primaryOnly, emptyPanel}, mon, kPad, kSlack);
    if (!Check(IsRectEmpty(&bOff) != FALSE, "off-monitor panel should produce empty copy bounds"))
        return 1;
    if (!Check(CopyBoundsCoverPanels(b1, {single, primaryOnly, emptyPanel}, mon, kPad), "off-monitor panels should be ignored"))
        return 1;
    if (!Check(!CopyBoundsCoverPanels(RECT{}, {single}, mon, kPad), "empty copy bounds must not cover visible panel"))
        return 1;

    return 0;
}
