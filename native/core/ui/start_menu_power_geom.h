// Transform helpers aligning Start menu power card hit boxes with drawn visuals.
#pragma once

#include <d2d1.h>

namespace atrium {

// Scales a rectangle about (anchorX, anchorY) to match Canvas::BeginGroup's transform.
inline D2D1_RECT_F ScaleRectAbout(D2D1_RECT_F r, float anchorX, float anchorY, float scale)
{
    return D2D1::RectF(anchorX + (r.left - anchorX) * scale, anchorY + (r.top - anchorY) * scale, anchorX + (r.right - anchorX) * scale,
                       anchorY + (r.bottom - anchorY) * scale);
}

} // namespace atrium
