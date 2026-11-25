#include "ui/widget_refresh_policy.h"

#include <iostream>

using atrium::ShouldSampleDisk;
using atrium::WidgetKind;
using atrium::WidgetNeedsMinuteRefresh;

namespace {

bool Require(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

bool CheckMinuteRefreshPolicy()
{
    struct Case {
        WidgetKind kind;
        bool expected;
        const char* name;
    };
    const Case cases[] = {
        {WidgetKind::Clock, true, "Clock needs minute refresh"},
        {WidgetKind::Digital, true, "Digital needs minute refresh"},
        {WidgetKind::Calendar, true, "Calendar needs minute refresh"},
        {WidgetKind::Battery, false, "Battery does not need minute refresh"},
        {WidgetKind::System, false, "System does not need minute refresh"},
        {WidgetKind::Notes, false, "Notes does not need minute refresh"},
        {WidgetKind::Reminders, false, "Reminders does not need minute refresh"},
        {WidgetKind::Photos, false, "Photos does not need minute refresh"},
        {WidgetKind::Weather, false, "Weather does not need minute refresh"},
        {WidgetKind::Bluetooth, false, "Bluetooth does not need minute refresh"},
        {WidgetKind::Network, false, "Network does not need minute refresh"},
    };
    for (const auto& c : cases) {
        if (!Require(WidgetNeedsMinuteRefresh(c.kind) == c.expected, c.name))
            return false;
    }
    return true;
}

bool CheckDiskSamplingCadence()
{
    return Require(ShouldSampleDisk(false, 0, 0), "unsampled disk samples immediately at t=0") &&
           Require(!ShouldSampleDisk(true, 2000, 0), "disk is not re-sampled at t=2000ms") &&
           Require(!ShouldSampleDisk(true, 29999, 0), "disk is not re-sampled at t=29999ms") &&
           Require(ShouldSampleDisk(true, 30000, 0), "disk re-samples at t=30000ms") &&
           Require(!ShouldSampleDisk(true, 30999, 1000), "disk respects non-zero baseline before 30000ms") &&
           Require(ShouldSampleDisk(true, 31000, 1000), "disk re-samples from non-zero baseline at 30000ms");
}

} // namespace

int main()
{
    if (!CheckMinuteRefreshPolicy() || !CheckDiskSamplingCadence())
        return 1;
    std::cout << "widget refresh: all checks passed\n";
    return 0;
}
