#include "ui/start_menu.h"

#include "clock.h"

#include <algorithm>
#include <cmath>

namespace atrium {

namespace {

constexpr float kRowH = 52;
constexpr float kListTop = 84;

bool InRect(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

} // namespace

bool StartMenu::HandleMouseMove(WPARAM w, POINT pt)
{
    if (sizing_) {
        POINT p;
        GetCursorPos(&p);
        Resize(p);
        return true;
    }
    if (scrubbing_) {
        ScrubTo(float(pt.y) / surface_.Scale());
        return true;
    }
    if (drag_.armed && !drag_.on && (w & MK_LBUTTON) &&
        (std::abs(pt.x - pressPt_.x) > GetSystemMetrics(SM_CXDRAG) || std::abs(pt.y - pressPt_.y) > GetSystemMetrics(SM_CYDRAG))) {
        drag_.armed = false;
        BeginDrag();
    }
    if (drag_.on) {
        UpdateDrag(pt);
        return true;
    }
    if (!tracking_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, surface_.Hwnd(), 0};
        tracking_ = TrackMouseEvent(&tme) != FALSE;
        surface_.Invalidate();
    }
    std::wstring h = HitAt(pt);
    if (h != hover_) {
        hover_ = h;
        surface_.Invalidate();
    }
    return true;
}

bool StartMenu::HandleLeftDown(POINT pt, POINT content)
{
    pressed_ = HitAt(pt);
    if (pressed_ != L"clear")
        searchFocused_ = pressed_ == L"search";
    if (powerOpen_ && pressed_ != L"power" && !pressed_.starts_with(L"pw:"))
        return DismissPowerCard();
    if (naming_ && pressed_ != namingHit_)
        FinishNaming(true);
    const float s = surface_.Scale(), x = float(pt.x) / s, y = float(pt.y) / s;
    if (folder_ >= 0 && !folderClosing_ && y < MainRect().bottom && !InRect(FolderRect(), x, y) && pressed_ != L"resize") {
        CloseFolder();
        pressed_.clear();
        return true;
    }
    if (pressed_ == L"azbar") {
        scrubbing_ = true;
        SetCapture(surface_.Hwnd());
        hintY_ = y;
        ScrubTo(y);
        return true;
    }
    if (pressed_ == L"resize") {
        sizing_ = true;
        GetCursorPos(&sizeFrom_);
        sizeStart_ = grid_;
        SetCapture(surface_.Hwnd());
        return true;
    }
    if ((pressed_.starts_with(L"tile:") && folder_ < 0) || (pressed_.starts_with(L"fapp:") && folder_ >= 0 && !folderClosing_)) {
        drag_.armed = true;
        pressPt_ = pt;
        SetCapture(surface_.Hwnd());
    }
    if (!pressed_.empty() && pressed_ != L"search" && pressed_ != L"fcard") {
        rippleId_ = pressed_;
        rippleX_ = x;
        rippleY_ = y;
        rippleMs_ = ClockMs();
        surface_.Ripple(float(content.x) / s, float(content.y) / s);
    }
    surface_.Invalidate();
    return true;
}

bool StartMenu::HandleLeftUp(POINT pt)
{
    if (scrubbing_) {
        scrubbing_ = false;
        pressed_.clear();
        if (GetCapture() == surface_.Hwnd())
            ReleaseCapture();
        surface_.Invalidate();
        return true;
    }
    if (sizing_) {
        EndResize();
        pressed_.clear();
        return true;
    }
    if (drag_.on) {
        pressed_.clear();
        EndDrag(true);
        return true;
    }
    if (drag_.armed) {
        drag_.armed = false;
        if (GetCapture() == surface_.Hwnd())
            ReleaseCapture();
    }
    std::wstring h = HitAt(pt);
    bool same = !h.empty() && h == pressed_;
    pressed_.clear();
    if (same)
        Activate(h);
    else
        surface_.Invalidate();
    return true;
}

bool StartMenu::HandleMouseWheel(UINT m, WPARAM w, POINT pt)
{
    POINT client = surface_.ScreenToContent(pt);
    client.x -= OffsetXPx();
    client.y -= OffsetPx();
    float notches = float(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA;
    if (m == WM_MOUSEWHEEL && HitAt(client) == L"volume") {
        status_.volume = AdjustVolume(int(std::lround(2 * notches)));
        if (notches > 0)
            status_.muted = false;
        if (onStatusRequest)
            onStatusRequest();
    } else if (folder_ >= 0) {
        wheelAcc_ += m == WM_MOUSEWHEEL ? -notches : notches;
        if (std::fabs(wheelAcc_) >= 1) {
            folderPage_ = std::clamp(folderPage_ + (wheelAcc_ > 0 ? 1 : -1), 0, FolderPages() - 1);
            wheelAcc_ = 0;
        }
    } else if (Listing()) {
        if (m == WM_MOUSEWHEEL)
            scrollTarget_ = std::clamp(scrollTarget_ - notches * 2 * kRowH, 0.f, std::max(0.f, contentH_ - (ListBottom() - kListTop)));
    } else {
        wheelAcc_ += m == WM_MOUSEWHEEL ? -notches : notches;
        if (std::fabs(wheelAcc_) >= 1) {
            SetPage(page_ + (wheelAcc_ > 0 ? 1 : -1));
            wheelAcc_ = 0;
        }
    }
    surface_.Invalidate();
    return true;
}

bool StartMenu::HandleKeyDown(WPARAM w)
{
    if (naming_)
        return w == VK_LEFT || w == VK_RIGHT || w == VK_UP || w == VK_DOWN;
    if (powerOpen_ && (w == VK_UP || w == VK_DOWN)) {
        powerSel_ = std::clamp(powerSel_ < 0 ? 0 : powerSel_ + (w == VK_DOWN ? 1 : -1), 0, int(power_.size()) - 1);
        surface_.Invalidate();
        return true;
    }
    switch (w) {
    case VK_UP: MoveSelection(0, -1); return true;
    case VK_DOWN: MoveSelection(0, 1); return true;
    case VK_LEFT: MoveSelection(-1, 0); return true;
    case VK_RIGHT: MoveSelection(1, 0); return true;
    case VK_F2: StartNaming(); return true;
    case VK_PRIOR:
    case VK_NEXT:
        if (!Listing() && folder_ < 0) {
            SetPage(page_ + (w == VK_NEXT ? 1 : -1));
            return true;
        }
        return false;
    default: return false;
    }
}

void StartMenu::HandleEscapeKey()
{
    if (powerOpen_) {
        powerOpen_ = false;
        surface_.Invalidate();
    } else if (folder_ >= 0 && !folderClosing_) {
        CloseFolder();
    } else if (!query_.empty()) {
        Activate(L"clear");
    } else if (allApps_) {
        Activate(L"allapps");
    } else {
        Close(false);
    }
}

void StartMenu::HandleReturnKey()
{
    if (powerOpen_) {
        RunPower(powerSel_);
    } else if (folder_ >= 0 && !folderClosing_) {
        if (sel_ >= 0)
            Activate(L"fapp:" + std::to_wstring(sel_));
    } else if (Listing()) {
        const auto& rows = RowsShown();
        int i = sel_;
        if (i < 0 && !query_.empty())
            for (int k = 0; k < int(rows.size()) && i < 0; ++k)
                if (!rows[size_t(k)].letter)
                    i = k;
        if (i >= 0)
            Activate(L"row:" + std::to_wstring(i));
    } else if (sel_ >= 0) {
        Activate(L"tile:" + std::to_wstring(sel_));
    }
}

bool StartMenu::HandleChar(WPARAM w, LRESULT& result)
{
    wchar_t ch = wchar_t(w);
    result = 0;
    if (naming_)
        return NamingChar(ch);
    if (ch == 27) {
        HandleEscapeKey();
    } else if (ch == 13) {
        HandleReturnKey();
    } else if (ch == 8) {
        if (!query_.empty()) {
            query_.pop_back();
            results_ = Results();
            if (query_.empty())
                Restage();
        }
        sel_ = -1;
        scroll_ = scrollTarget_ = 0;
        surface_.Invalidate();
    } else if (ch >= 32 && query_.size() < 64) {
        searchFocused_ = true;
        CloseFolder();
        if (query_.empty())
            Restage();
        query_ += ch;
        results_ = Results();
        sel_ = -1;
        scroll_ = scrollTarget_ = 0;
        powerOpen_ = false;
        surface_.Invalidate();
        PlaceIme();
    }
    return true;
}

} // namespace atrium
