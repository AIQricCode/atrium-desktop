// Verifies IconImage shared pixel buffers, AppIconCache FIFO priority & budget eviction, and Launchpad #system expansion.
#include "icon_image.h"
#include "shell/app_icon_cache.h"
#include "shell/shell_icon.h"
#include "ui/launchpad_page_ids.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const char* msg)
{
    if (!ok) {
        std::cerr << "FAIL: " << msg << '\n';
        ++g_failures;
    }
}

atrium::IconImage MakeSolidImage(int side, size_t byteOverride = 0)
{
    const size_t bytes = byteOverride ? byteOverride : size_t(side) * size_t(side) * 4;
    return atrium::IconImage::FromBgra(side, side, std::vector<uint8_t>(bytes, 0x7F));
}

} // namespace

int main()
{
    using namespace atrium;

    // 1. IconImage shared read-only pixels & FitToSquare zero-copy fast path.
    const IconImage orig = MakeSolidImage(64);
    IconImage copy;
    copy = orig;
    const IconImage fitSame = FitToSquare(orig, 64);
    Check(!orig.Empty() && orig.ByteSize() == 64 * 64 * 4, "IconImage reports exact byte size");
    Check(copy.Data() == orig.Data() && fitSame.Data() == orig.Data(), "IconImage copy and same-size FitToSquare share buffer");

    // 2. Priority lanes: FIFO within tier, Visible > Ahead > Prefetch, and rapid SetAhead replacement.
    AppIconCache cache;
    cache.QueueIcon({L"pf1", 64}, IconJobTier::Prefetch, 100);
    cache.QueueIcon({L"pf2", 64}, IconJobTier::Prefetch, 100);
    cache.SetAhead({L"stale1", L"stale2"}, 64, 100);
    // Flip page before stale1/stale2 ran: replace with adj1/adj2 and promote pf2 to Visible.
    cache.SetAhead({L"adj1", L"adj2"}, 64, 101);
    cache.QueueIcon({L"pf2", 64}, IconJobTier::Visible, 101);

    IconKey popped;
    std::vector<std::wstring> order;
    while (cache.PopNextJob(popped)) {
        order.push_back(popped.first);
        cache.activeJob.reset();
    }
    const std::vector<std::wstring> expectedOrder = {L"pf2", L"adj1", L"adj2", L"pf1"};
    Check(order == expectedOrder, "queue must decode Visible first, fresh Ahead next, FIFO Prefetch last, dropping stale Ahead");

    // 3. Incremental cacheBytes accounting, SetVisible no-op short circuit, stale sweep, and 512 / 16 MiB eviction.
    cache = {};
    for (int i = 0; i < 600; ++i) {
        const std::wstring id = L"app" + std::to_wstring(i);
        cache.liveIds.insert(id);
        cache.InsertIcon({id, 64}, MakeSolidImage(40, 40 * 1024));
    }
    Check(cache.cacheBytes == 600ULL * 40ULL * 1024ULL, "cacheBytes tracks inserts accurately");

    // Replace one icon with double size.
    cache.InsertIcon({L"app0", 64}, MakeSolidImage(40, 80 * 1024));
    Check(cache.cacheBytes == 599ULL * 40ULL * 1024ULL + 80ULL * 1024ULL, "cacheBytes tracks replacement size delta");

    Check(cache.SetVisible(0, {{L"app0", 64}, {L"app1", 64}}), "first SetVisible returns changed");
    Check(!cache.SetVisible(0, {{L"app1", 64}, {L"app0", 64}}), "unchanged visible set short-circuits");
    cache.SetAhead({L"app2"}, 64, 200);

    cache.Prune(true, true, [](const std::wstring&) { return false; });
    Check(cache.icons.size() <= kIconCacheMaxCount && cache.cacheBytes <= kIconCacheMaxBytes, "Prune enforces 16 MiB and 512-entry limits");
    Check(cache.icons.contains({L"app0", 64}) && cache.icons.contains({L"app1", 64}) && cache.icons.contains({L"app2", 64}),
          "visible and narrowed ahead icons stay protected across eviction");
    Check(!cache.icons.contains({L"app3", 64}), "oldest unprotected prewarmed entry is evicted");

    // Verify stale sweep updates cacheBytes accurately.
    const size_t beforeSweep = cache.cacheBytes;
    cache.liveIds.erase(L"app599");
    const bool had599 = cache.icons.contains({L"app599", 64});
    cache.Prune(true, true, [](const std::wstring&) { return false; });
    if (had599)
        Check(cache.cacheBytes == beforeSweep - 40ULL * 1024ULL && !cache.icons.contains({L"app599", 64}),
              "stale sweep removes unreferenced icon and subtracts its bytes");

    // 4. Launchpad #system folder expansion.
    std::vector<CatalogApp> shown = {{L"A", L"a"}, {L"System", L"#system"}, {L"B", L"b"}};
    std::vector<CatalogApp> sys;
    sys.reserve(12);
    for (int i = 0; i < 12; ++i)
        sys.push_back({L"Sys" + std::to_wstring(i), L"sys" + std::to_wstring(i)});
    const std::vector<std::wstring> pageIds = ExpandLaunchpadPageIds(shown, sys, 0, 10, L"#system");
    Check(pageIds.size() == 11 && pageIds[0] == L"a" && pageIds[1] == L"sys0" && pageIds[9] == L"sys8" && pageIds[10] == L"b",
          "ExpandLaunchpadPageIds expands #system into its first 9 apps");

    return g_failures ? 1 : 0;
}
