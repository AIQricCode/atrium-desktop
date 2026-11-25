// Wire format structs and binary helpers for Shell_NotifyIcon WM_COPYDATA calls.
#pragma once

#include "icon_image.h"

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace atrium::tray_wire {

inline constexpr DWORD kSignature = 0x34753423;

// The shell's wire format (WM_COPYDATA dwData 1): handles as 32-bit values, the
// same for 32- and 64-bit apps.
struct NotifyIconData32 {
    DWORD cbSize;
    DWORD hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    DWORD hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    UINT uVersion; // also uTimeout
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    DWORD hBalloonIcon;
};

struct TrayNotifyData {
    DWORD signature;
    DWORD message;
    NotifyIconData32 nid;
};

// Shell_NotifyIconGetRect (WM_COPYDATA dwData 3).
struct NotifyIconIdentifier32 {
    DWORD signature;
    DWORD message; // 1: the top left corner, 2: the bottom right
    DWORD cbSize;
    DWORD padding;
    DWORD hWnd;
    UINT uID;
    GUID guidItem;
};

inline HWND Handle(DWORD value) { return reinterpret_cast<HWND>(static_cast<ULONG_PTR>(value)); }

inline uint64_t HashIconImage(const IconImage& img)
{
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(uint64_t(img.width));
    mix(uint64_t(img.height));
    for (uint8_t b : img.Bytes())
        mix(b);
    return h;
}

inline std::wstring KeyOf(HWND hwnd, UINT uid, const GUID* guid)
{
    wchar_t key[64];
    if (guid && *guid != GUID{}) {
        StringFromGUID2(*guid, key, 64);
        return L"guid:" + std::wstring(key);
    }
    swprintf_s(key, L"hwnd:%p:%u", static_cast<void*>(hwnd), uid);
    return key;
}

} // namespace atrium::tray_wire
