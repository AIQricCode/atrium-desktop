// Expands Launchpad page entries (including the system folder preview) into catalog icon IDs.
#pragma once

#include "shell/app_catalog.h"
#include "ui/launchpad_model.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace atrium {

inline std::vector<std::wstring> ExpandLaunchpadPageIds(const std::vector<CatalogApp>& shown, const std::vector<CatalogApp>& system, int page, int perPage,
                                                        std::wstring_view folderId)
{
    std::vector<std::wstring> ids;
    const LaunchpadSlice slice = PageSlice(shown.size(), page, perPage);
    ids.reserve(slice.end - slice.begin);
    for (size_t i = slice.begin; i < slice.end; ++i) {
        if (shown[i].id == folderId) {
            for (size_t j = 0; j < std::min<size_t>(9, system.size()); ++j)
                ids.push_back(system[j].id);
        } else {
            ids.push_back(shown[i].id);
        }
    }
    return ids;
}

} // namespace atrium
