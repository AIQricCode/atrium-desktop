// Verifies AppCatalog::PrepareQuery + MatchesPrepared parity against AppCatalog::Matches.
#include "localization.h"
#include "shell/app_catalog.h"

#include <iostream>
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

} // namespace

int main()
{
    using namespace atrium;
    SetLanguage(L"en");

    const std::vector<CatalogApp> apps = {
        {L"微信", L"WeChat.exe", L"", L"C:\\Program Files\\Tencent\\WeChat\\WeChat.exe"},
        {L"计算器", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App", L"", L""},
        {L"Notepad", L"C:\\Windows\\notepad.exe", L"", L"C:\\Windows\\notepad.exe"},
        {L"Visual Studio Code", L"VSCode", L"", L"D:\\Apps\\VSCode\\Code.exe"},
    };
    const std::vector<std::wstring> queries = {L"", L"wx", L"WX", L"calc", L"ca", L"notepad", L"note", L"cod", L"vs", L"exe", L"xy", L"微信"};

    for (const auto& q : queries) {
        const AppCatalog::PreparedQuery pq = AppCatalog::PrepareQuery(q);
        for (const auto& app : apps)
            Check(AppCatalog::MatchesPrepared(app, pq) == AppCatalog::Matches(app, q), "PreparedQuery matches original Matches()");
    }
    Check(AppCatalog::MatchesPrepared(apps[0], AppCatalog::PrepareQuery(L"wx")), "Chinese initials match works");
    Check(AppCatalog::MatchesPrepared(apps[1], AppCatalog::PrepareQuery(L"calc")), "package tail match (>=3 chars) works");
    Check(!AppCatalog::MatchesPrepared(apps[2], AppCatalog::PrepareQuery(L"ex")), "<3 char query does not match extension tail");

    return g_failures ? 1 : 0;
}
