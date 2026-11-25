// Pure refresh-cadence and disk-sampling policy for desktop widgets.
#pragma once

#include "ui/widget_faces.h"

#include <windows.h>

namespace atrium {

inline bool WidgetNeedsMinuteRefresh(WidgetKind kind) { return kind == WidgetKind::Clock || kind == WidgetKind::Digital || kind == WidgetKind::Calendar; }

inline bool ShouldSampleDisk(bool sampledOnce, ULONGLONG nowMs, ULONGLONG lastSampleMs, ULONGLONG intervalMs = 30000)
{
    return !sampledOnce || (nowMs - lastSampleMs) >= intervalMs;
}

} // namespace atrium
