// Icon decode priority queues and bounded LRU cache backing AppCatalog.
#pragma once

#include "icon_image.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace atrium {

inline constexpr size_t kIconCacheMaxBytes = 16 * 1024 * 1024;
inline constexpr size_t kIconCacheMaxCount = 512;

enum class IconJobTier { Visible = 0, Ahead = 1, Prefetch = 2 };

using IconKey = std::pair<std::wstring, int>;

struct CachedIcon {
    IconImage image;
    uint64_t used = 0;
};

struct FailedIcon {
    unsigned attempts = 0;
    ULONGLONG time = 0;
};

struct AppIconCache {
    std::map<IconKey, CachedIcon> icons;
    std::map<IconKey, FailedIcon> failed;
    std::array<std::deque<IconKey>, 3> jobs;
    std::map<IconKey, IconJobTier> queuedTier;
    std::optional<IconKey> activeJob;
    std::set<IconKey> aheadIcons;
    std::array<std::set<IconKey>, 2> visibleIcons;
    std::set<std::wstring> liveIds, startIds;
    int prefetchPx = 0;
    size_t cacheBytes = 0;
    uint64_t iconUse = 0;

    bool IsRequested(const IconKey& key) const { return queuedTier.contains(key) || (activeJob && *activeJob == key); }

    void InsertIcon(const IconKey& key, IconImage image)
    {
        failed.erase(key);
        const size_t nextBytes = image.ByteSize();
        if (auto it = icons.find(key); it != icons.end()) {
            cacheBytes -= it->second.image.ByteSize();
            cacheBytes += nextBytes;
            it->second = {std::move(image), ++iconUse};
            return;
        }
        cacheBytes += nextBytes;
        icons.emplace(key, CachedIcon{std::move(image), ++iconUse});
    }

    std::map<IconKey, CachedIcon>::iterator EraseIcon(std::map<IconKey, CachedIcon>::iterator it)
    {
        cacheBytes -= it->second.image.ByteSize();
        return icons.erase(it);
    }

    bool QueueIcon(const IconKey& key, IconJobTier tier, ULONGLONG nowMs)
    {
        if (activeJob && *activeJob == key)
            return false;
        if (auto fit = failed.find(key); fit != failed.end() && (fit->second.attempts >= 3 || nowMs - fit->second.time < 2000))
            return false;
        if (auto qt = queuedTier.find(key); qt != queuedTier.end()) {
            if (int(tier) >= int(qt->second))
                return false;
            qt->second = tier;
            jobs[size_t(tier)].push_back(key);
            return true;
        }
        queuedTier.emplace(key, tier);
        jobs[size_t(tier)].push_back(key);
        return true;
    }

    bool PopNextJob(IconKey& out)
    {
        for (size_t tier = 0; tier < jobs.size(); ++tier) {
            auto& q = jobs[tier];
            while (!q.empty()) {
                IconKey key = std::move(q.front());
                q.pop_front();
                const auto qt = queuedTier.find(key);
                if (qt == queuedTier.end() || size_t(qt->second) != tier || icons.contains(key))
                    continue;
                queuedTier.erase(qt);
                activeJob = key;
                out = std::move(key);
                return true;
            }
        }
        return false;
    }

    bool SetVisible(size_t ownerIndex, const std::vector<IconKey>& visible)
    {
        std::set<IconKey> next(visible.begin(), visible.end());
        if (visibleIcons[ownerIndex] == next)
            return false;
        visibleIcons[ownerIndex] = std::move(next);
        return true;
    }

    bool SetAhead(const std::vector<std::wstring>& ids, int px, ULONGLONG nowMs)
    {
        aheadIcons.clear();
        auto& aheadQueue = jobs[size_t(IconJobTier::Ahead)];
        for (const IconKey& key : aheadQueue) {
            if (auto qt = queuedTier.find(key); qt != queuedTier.end() && qt->second == IconJobTier::Ahead)
                queuedTier.erase(qt);
        }
        aheadQueue.clear();
        bool queued = false;
        for (const std::wstring& id : ids) {
            IconKey key{id, px};
            aheadIcons.insert(key);
            if (!icons.contains(key))
                queued |= QueueIcon(key, IconJobTier::Ahead, nowMs);
        }
        return queued;
    }

    bool HasWork() const
    {
        return std::ranges::any_of(jobs, [](const auto& q) { return !q.empty(); });
    }

    template<class PinCheck> void Prune(bool loaded, bool sweepStale, PinCheck&& isPinned)
    {
        auto isProtected = [&](const IconKey& key) {
            return visibleIcons[0].contains(key) || visibleIcons[1].contains(key) || IsRequested(key) || aheadIcons.contains(key) ||
                   (key.second == prefetchPx && (startIds.contains(key.first) || isPinned(key.first)));
        };
        if (sweepStale && loaded) {
            for (auto it = icons.begin(); it != icons.end();) {
                const bool stale = !liveIds.contains(it->first.first) && !startIds.contains(it->first.first) && !isProtected(it->first);
                it = stale ? EraseIcon(it) : std::next(it);
            }
            std::erase_if(failed, [&](const auto& entry) {
                return !liveIds.contains(entry.first.first) && !startIds.contains(entry.first.first) && !isProtected(entry.first);
            });
        }
        if (cacheBytes > kIconCacheMaxBytes || icons.size() > kIconCacheMaxCount) {
            std::vector<decltype(icons)::iterator> victims;
            victims.reserve(icons.size());
            for (auto it = icons.begin(); it != icons.end(); ++it)
                if (!isProtected(it->first))
                    victims.push_back(it);
            std::ranges::sort(victims, {}, [](const auto& it) { return it->second.used; });
            for (auto it : victims) {
                if (cacheBytes <= kIconCacheMaxBytes && icons.size() <= kIconCacheMaxCount)
                    break;
                EraseIcon(it);
            }
        }
        while (failed.size() > kIconCacheMaxCount) {
            auto victim = std::ranges::min_element(failed, [](const auto& a, const auto& b) { return a.second.time < b.second.time; });
            failed.erase(victim);
        }
    }
};

} // namespace atrium
