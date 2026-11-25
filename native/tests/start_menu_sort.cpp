#include "shell/app_catalog.h"
#include "shell/start_layout.h"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace atrium;

namespace {

constexpr wchar_t kHiddenHead = 1;
int g_failures = 0;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

int LetterRank(wchar_t letter) { return letter == L'#' ? 0 : letter >= L'A' && letter <= L'Z' ? letter - L'A' + 1 : 27; }

bool CompareAppNames(const std::wstring& a, const std::wstring& b)
{
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), int(a.size()), b.c_str(), int(b.size()), nullptr,
                           nullptr, 0) == CSTR_LESS_THAN;
}

struct TestTile {
    std::wstring name;
    bool folder = false;
};

struct TestRow {
    wchar_t letter = 0;
    int folder = -1;
    CatalogApp app;
};

std::vector<TestRow> BuildAllApps(const std::vector<TestTile>& tiles, const std::vector<CatalogApp>& apps, const std::vector<std::wstring>& hiddenIds)
{
    struct Entry {
        wchar_t letter;
        TestRow row;
    };
    auto isHidden = [&](const std::wstring& id) { return std::ranges::find(hiddenIds, id) != hiddenIds.end(); };
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
            return LetterRank(a.letter) < LetterRank(b.letter);
        return CompareAppNames(nameOf(a), nameOf(b));
    });
    std::vector<TestRow> out;
    wchar_t letter = 0;
    for (Entry& e : entries) {
        if (e.letter != letter) {
            letter = e.letter;
            out.push_back({letter, -1, {}});
        }
        out.push_back(std::move(e.row));
    }
    if (!hidden.empty()) {
        std::ranges::stable_sort(hidden, [](const CatalogApp& a, const CatalogApp& b) { return CompareAppNames(a.name, b.name); });
        out.push_back({kHiddenHead, -1, {}});
        for (CatalogApp& a : hidden)
            out.push_back({0, -1, std::move(a)});
    }
    return out;
}

std::wstring RowName(const std::vector<TestTile>& tiles, const TestRow& row)
{
    if (row.letter)
        return row.letter == kHiddenHead ? L"[HIDDEN]" : std::wstring(1, row.letter);
    return row.folder >= 0 ? tiles[size_t(row.folder)].name : row.app.name;
}

void CheckLargeMixedSort()
{
    std::vector<TestTile> tiles = {
        {L"7-Zip", true},
        {L"Alpha Tools", true},
        {L"App 3", true},
        {L"Dev Suite", true},
        {L"Shared Name", true},
        {L"Utilities", true},
        {L"\u5de5\u5177\u7bb1", true}, // 工具箱 (G)
        {L"\u529e\u516c\u8f6f\u4ef6", true}, // 办公软件 (B)
        {L"NonFolderTile", false},
    };

    std::vector<CatalogApp> apps;
    auto addApp = [&](const std::wstring& name, const std::wstring& id) {
        CatalogApp a;
        a.name = name;
        a.id = id;
        apps.push_back(std::move(a));
    };

    // > 64 total entries in unsorted order so std::ranges::stable_sort exercises merge buffer moves.
    addApp(L"Zed", L"zed");
    addApp(L"App 10", L"app10");
    addApp(L"App 2", L"app2");
    addApp(L"app 20", L"app20");
    addApp(L"App 1", L"app1");
    addApp(L"Archive", L"archive");
    addApp(L"Shared Name", L"shared_app_1");
    addApp(L"Shared Name", L"shared_app_2");
    addApp(L"1Password", L"1password");
    addApp(L"3D Viewer", L"3dviewer");
    addApp(L"\u5fae\u4fe1", L"wechat");       // 微信 (W)
    addApp(L"\u7f51\u6613\u4e91\u97f3\u4e50", L"netease"); // 网易云音乐 (W)
    addApp(L"\u652f\u4ed8\u5b9d", L"alipay"); // 支付宝 (Z)
    addApp(L"\u9489\u9489", L"dingtalk");     // 钉钉 (D)
    addApp(L"\u817e\u8baf\u4f1a\u8bae", L"meeting"); // 腾讯会议 (T)
    addApp(L"Hidden 10", L"hid10");
    addApp(L"Hidden 2", L"hid2");

    for (int i = 60; i >= 1; --i) {
        const wchar_t letter = static_cast<wchar_t>(L'B' + (i % 24));
        addApp(std::wstring(1, letter) + L" Item " + std::to_wstring(i), L"bulk_" + std::to_wstring(i));
    }

    const std::vector<std::wstring> hiddenIds = {L"hid10", L"hid2"};
    const std::vector<TestRow> rows = BuildAllApps(tiles, apps, hiddenIds);

    // Collect every non-heading entry in order and verify strict non-decreasing sort invariant.
    std::vector<std::pair<wchar_t, std::wstring>> sortedItems;
    std::vector<std::wstring> hiddenNames;
    bool inHidden = false;
    wchar_t prevHeading = 0;
    for (const TestRow& row : rows) {
        if (row.letter) {
            if (row.letter == kHiddenHead) {
                inHidden = true;
                continue;
            }
            Require(!inHidden, "no letter headings appear after hidden heading");
            if (prevHeading)
                Require(LetterRank(prevHeading) < LetterRank(row.letter), "letter headings are strictly ordered (# then A..Z)");
            prevHeading = row.letter;
            continue;
        }
        const std::wstring name = RowName(tiles, row);
        if (inHidden)
            hiddenNames.push_back(name);
        else
            sortedItems.emplace_back(prevHeading, name);
    }

    Require(sortedItems.size() == 83, "all 8 non-empty folders and 75 visible apps appear");
    Require(hiddenNames == std::vector<std::wstring>{L"Hidden 2", L"Hidden 10"}, "hidden apps sort with natural digit order");

    for (size_t i = 1; i < sortedItems.size(); ++i) {
        const auto& [prevLetter, prevName] = sortedItems[i - 1];
        const auto& [curLetter, curName] = sortedItems[i];
        if (prevLetter != curLetter) {
            Require(LetterRank(prevLetter) < LetterRank(curLetter), "letters strictly increase across groups");
        } else {
            Require(!CompareAppNames(curName, prevName), "within-letter entries are non-decreasing under CompareAppNames");
        }
    }

    auto indexOf = [&](const std::wstring& targetName, int expectedFolder = -2, const std::wstring& expectedId = L"") {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].letter)
                continue;
            if (expectedFolder != -2 && rows[i].folder != expectedFolder)
                continue;
            if (!expectedId.empty() && rows[i].app.id != expectedId)
                continue;
            if (RowName(tiles, rows[i]) == targetName)
                return int(i);
        }
        return -1;
    };

    const int i1p = indexOf(L"1Password");
    const int i3d = indexOf(L"3D Viewer");
    const int i7z = indexOf(L"7-Zip");
    const int iApp1 = indexOf(L"App 1");
    const int iApp2 = indexOf(L"App 2");
    const int iApp3 = indexOf(L"App 3");
    const int iApp10 = indexOf(L"App 10");
    const int iApp20 = indexOf(L"app 20");
    const int iAlpha = indexOf(L"Alpha Tools");
    const int iArchive = indexOf(L"Archive");
    const int iSharedFolder = indexOf(L"Shared Name", 4);
    const int iSharedApp1 = indexOf(L"Shared Name", -1, L"shared_app_1");
    const int iSharedApp2 = indexOf(L"Shared Name", -1, L"shared_app_2");

    Require(i1p >= 0 && i3d > i1p && i7z > i3d && iAlpha > i7z, "# group precedes A and sorts numerically");
    Require(iAlpha < iApp1 && iApp1 < iApp2 && iApp2 < iApp3 && iApp3 < iApp10 && iApp10 < iApp20 && iApp20 < iArchive,
            "mixed folders and apps under A sort naturally with digits as numbers");
    Require(iSharedFolder >= 0 && iSharedFolder < iSharedApp1 && iSharedApp1 < iSharedApp2,
            "stable_sort preserves relative order of folder and apps sharing a name");
    Require(indexOf(L"NonFolderTile") == -1, "non-folder tiles are excluded from All apps folder rows");
}

void CheckIndexLetters()
{
    Require(IndexLetter(L"  alpha") == L'A', "leading whitespace skipped and lowercase uppercased");
    Require(IndexLetter(L"\uFF3Aebra") == L'Z', "full-width Latin maps to ASCII uppercase");
    Require(IndexLetter(L"7-Zip") == L'#', "digit maps to #");
    Require(IndexLetter(L"") == L'#', "empty name maps to #");
    Require(IndexLetter(L"\u5fae\u4fe1") == L'W', "Chinese name maps to Pinyin initial");
}

} // namespace

int main()
{
    CheckIndexLetters();
    CheckLargeMixedSort();
    if (g_failures != 0)
        return 1;
    std::cout << "start menu sort checks passed\n";
    return 0;
}
