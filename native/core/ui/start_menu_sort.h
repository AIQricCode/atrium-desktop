// All-apps A-Z grouping and natural-order sorting shared by StartMenu and its unit test.
#pragma once

#include "shell/app_catalog.h"
#include "shell/start_layout.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace atrium {

inline constexpr wchar_t kStartMenuHiddenHead = 1; // the heading of the hidden apps, after Z

inline int StartMenuLetterRank(wchar_t letter)
{
    return letter == L'#' ? 0 : letter >= L'A' && letter <= L'Z' ? letter - L'A' + 1 : 27;
}

inline bool CompareStartMenuAppNames(const std::wstring& a, const std::wstring& b)
{
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), int(a.size()), b.c_str(), int(b.size()), nullptr,
                           nullptr, 0) == CSTR_LESS_THAN;
}

template <typename RowT, typename TileT, typename IsHiddenFn>
std::vector<RowT> BuildStartMenuAllAppsRows(const std::vector<TileT>& tiles, const std::vector<CatalogApp>& apps, IsHiddenFn&& isHidden)
{
    // Every app and folder under its letter (# first, as in Windows), by name within it.
    struct Entry {
        wchar_t letter;
        RowT row;
    };
    std::vector<Entry> entries;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].folder)
            entries.push_back({IndexLetter(tiles[i].name), {0, int(i), {}}});
    std::vector<CatalogApp> hidden;
    for (const CatalogApp& a : apps) {
        if (isHidden(a.id))
            hidden.push_back(a);
        else
            entries.push_back({IndexLetter(a.name), {0, -1, a}});
    }
    auto nameOf = [&](const Entry& e) -> const std::wstring& { return e.row.folder >= 0 ? tiles[size_t(e.row.folder)].name : e.row.app.name; };
    std::ranges::stable_sort(entries, [&](const Entry& a, const Entry& b) {
        if (a.letter != b.letter)
            return StartMenuLetterRank(a.letter) < StartMenuLetterRank(b.letter);
        return CompareStartMenuAppNames(nameOf(a), nameOf(b));
    });
    std::vector<RowT> out;
    wchar_t letter = 0;
    for (Entry& e : entries) {
        if (e.letter != letter) {
            letter = e.letter;
            out.push_back({letter, -1, {}});
        }
        out.push_back(std::move(e.row));
    }
    // The hidden apps last, under a heading of their own.
    if (!hidden.empty()) {
        std::ranges::stable_sort(hidden, [](const CatalogApp& a, const CatalogApp& b) { return CompareStartMenuAppNames(a.name, b.name); });
        out.push_back({kStartMenuHiddenHead, -1, {}});
        for (CatalogApp& a : hidden)
            out.push_back({0, -1, std::move(a)});
    }
    return out;
}

} // namespace atrium
