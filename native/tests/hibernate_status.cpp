// Verifies hibernate capability classification, shared menu description, and status-to-notice feedback mapping.
#include "localization.h"
#include "shell/system_status.h"

#include <iostream>

namespace {

int g_failures = 0;

void Check(bool ok, const char* msg)
{
    if (!ok) {
        std::cerr << "FAIL: " << msg << '\n';
        ++g_failures;
    }
}

} // namespace

int main()
{
    using namespace atrium;

    SetLanguage(L"en");

    SYSTEM_POWER_CAPABILITIES caps{};
    Check(ClassifyHibernateCapability(false, caps) == HibernateCapability::QueryFailed, "query failure must classify as QueryFailed");

    caps.SystemS4 = FALSE;
    caps.HiberFilePresent = FALSE;
    Check(ClassifyHibernateCapability(true, caps) == HibernateCapability::Unsupported, "SystemS4=FALSE must be Unsupported");

    caps.SystemS4 = TRUE;
    caps.HiberFilePresent = FALSE;
    Check(ClassifyHibernateCapability(true, caps) == HibernateCapability::NeedsEnable, "SystemS4=TRUE, HiberFilePresent=FALSE must be NeedsEnable");

    caps.SystemS4 = TRUE;
    caps.HiberFilePresent = TRUE;
    caps.HiberFileType = 2;
    Check(ClassifyHibernateCapability(true, caps) == HibernateCapability::Direct, "SystemS4=TRUE, HiberFilePresent=TRUE, HiberFileType=2 must be Direct");

    caps.SystemS4 = TRUE;
    caps.HiberFilePresent = TRUE;
    caps.HiberFileType = 1;
    Check(ClassifyHibernateCapability(true, caps) == HibernateCapability::NeedsEnable,
          "SystemS4=TRUE, HiberFilePresent=TRUE, HiberFileType=1 (reduced) must be NeedsEnable");

    caps.SystemS4 = TRUE;
    caps.HiberFilePresent = TRUE;
    caps.HiberFileType = 0;
    Check(ClassifyHibernateCapability(true, caps) == HibernateCapability::NeedsEnable,
          "SystemS4=TRUE, HiberFilePresent=TRUE, HiberFileType=0 (none) must be NeedsEnable");

    const HibernateMenuOption direct = DescribeHibernateMenu(HibernateCapability::Direct);
    Check(direct.visible && !direct.needsEnable && direct.label == L"Hibernate", "Direct menu item must say Hibernate");

    const HibernateMenuOption enable = DescribeHibernateMenu(HibernateCapability::NeedsEnable);
    Check(enable.visible && enable.needsEnable && enable.label == L"Enable hibernation and hibernate",
          "NeedsEnable menu item must say Enable hibernation and hibernate");

    const HibernateMenuOption unsupported = DescribeHibernateMenu(HibernateCapability::Unsupported);
    Check(!unsupported.visible && !unsupported.needsEnable, "Unsupported must be hidden");

    const HibernateMenuOption failed = DescribeHibernateMenu(HibernateCapability::QueryFailed);
    Check(!failed.visible && !failed.needsEnable, "QueryFailed must not offer enabling hibernation");

    Check(!BuildPowerActionNotice(PowerAction::Hibernate, PowerActionStatus::Ok).has_value(), "Ok produces no failure notice");
    const auto cancelled = BuildPowerActionNotice(PowerAction::Hibernate, PowerActionStatus::Cancelled);
    Check(cancelled && cancelled->title == L"Hibernation cancelled", "Cancelled produces cancellation notice");
    const auto enableFailed = BuildPowerActionNotice(PowerAction::Hibernate, PowerActionStatus::EnableFailed);
    Check(enableFailed && enableFailed->title == L"Could not enable hibernation", "EnableFailed produces enable failure notice");
    const auto elevationFailed = BuildPowerActionNotice(PowerAction::Hibernate, PowerActionStatus::ElevationFailed);
    Check(elevationFailed && elevationFailed->title == L"Could not enable hibernation", "ElevationFailed produces enable failure notice");
    const auto hibernateFailed = BuildPowerActionNotice(PowerAction::Hibernate, PowerActionStatus::HibernateFailed);
    Check(hibernateFailed && hibernateFailed->title == L"Could not hibernate", "HibernateFailed produces hibernate failure notice");

    return g_failures ? 1 : 0;
}
