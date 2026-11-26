#include "ui/control_center.h"

#include "bar_edge.h"
#include "localization.h"
#include "render/wifi_glyph.h"
#include "ui/control_layout.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace atrium {

namespace {

using control::kGap;
using control::kPad;
using control::kRowH;
using control::kTopH;
using control::kW;

D2D1_COLOR_F Alpha(D2D1_COLOR_F c, float a) { return D2D1::ColorF(c.r, c.g, c.b, c.a * a); }

// Signal as the bar's three-step Wi-Fi symbol, in the 18 DIP from x.
void Bars(Canvas& c, float x, float cy, int signal, D2D1_COLOR_F ink) { c.WifiFan(x + 8, cy, 12, WifiSteps(signal), ink); }

// Win+P's pictures: two screens, which of them shows the desktop and how.
void ModePicture(Canvas& c, ProjectionMode m, float cx, float cy, D2D1_COLOR_F ink)
{
    const float sw = 17, sh = 12, gap = m == ProjectionMode::Extend ? 0.f : 5.f;
    const D2D1_RECT_F a = D2D1::RectF(cx - gap / 2 - sw, cy - sh / 2, cx - gap / 2, cy + sh / 2);
    const D2D1_RECT_F b = D2D1::RectF(cx + gap / 2, cy - sh / 2, cx + gap / 2 + sw, cy + sh / 2);
    const D2D1_COLOR_F off = Alpha(ink, 0.35f);
    switch (m) {
    case ProjectionMode::Internal:
        c.Fill(a, 2.5f, ink);
        c.Stroke(b, 2.5f, off, 1.2f);
        break;
    case ProjectionMode::External:
        c.Stroke(a, 2.5f, off, 1.2f);
        c.Fill(b, 2.5f, ink);
        break;
    case ProjectionMode::Clone:
        c.Fill(a, 2.5f, ink);
        c.Fill(b, 2.5f, ink);
        break;
    default: // extend: one desktop over both
        c.Fill(D2D1::RectF(a.left - 3, a.top, b.right + 3, b.bottom), 2.5f, ink);
        break;
    }
}

} // namespace

float ControlCenter::Hover(Canvas& c, const std::wstring& id, bool on)
{
    auto& f = hoverAnim_[id];
    const float v = f.Step(on, c.now);
    if (f.moving)
        c.Continue();
    return v;
}

void ControlCenter::PaintPage(Canvas& c, const Theme& t, Page page)
{
    if (page == Page::Main) {
        PaintMain(c, t);
        return;
    }
    const D2D1_RECT_F card = D2D1::RectF(0, 0, kW, HeightOf(page));
    if (c.panel)
        c.Fill(card, radius_, t.panel);
    if (page == Page::Wifi)
        PaintWifi(c, t, card);
    else if (page == Page::Bluetooth)
        PaintBluetooth(c, t, card);
    else if (page == Page::Sound)
        PaintSound(c, t, card);
    else
        PaintCast(c, t, card);
}

std::vector<ShapeDip> ControlCenter::BuildMorphShapes(D2D1_RECT_F card, float e) const
{
    // One piece of glass melting from the main page's pieces (k 0) into the card (k 1): the gaps close,
    // inner corners square up, outer corners round into the card's, and a smooth union bridges the gaps.
    const float k = fromPage_ == Page::Main ? e : 1 - e;
    const float mainH = MainHeight(), sy = shownH_ / mainH, g = kGap / 2 * k;
    ShapeDip m{card, radius_};
    m.melt = 22 * std::sin(std::numbers::pi_v<float> * k);
    m.partMix = k * k * (3 - 2 * k); // the seams fade as the parts become the card
    for (const ShapeDip& piece : MainShapes()) {
        const D2D1_RECT_F r = piece.rect;
        const bool atL = r.left<0.5f, atT = r.top<0.5f, atR = r.right> kW - 0.5f, atB = r.bottom> mainH - 0.5f;
        const float ql = atL ? r.left : r.left - g, qt = card.top + (atT ? r.top : r.top - g) * sy;
        const float qr = atR ? r.right : r.right + g, qb = card.top + (atB ? r.bottom : r.bottom + g) * sy;
        const D2D1_RECT_F q = D2D1::RectF(ql, qt, qr, qb);
        auto corner = [&](bool outer) { return piece.radius + ((outer ? radius_ : 0.f) - piece.radius) * k; };
        m.parts.push_back({q, {corner(atL && atT), corner(atR && atT), corner(atR && atB), corner(atL && atB)}});
    }
    return {std::move(m)};
}

// The glass stands on the window's bottom; its height glides to the page's.
// Between the main page and another, the main page's pieces of glass melt
// into one (or come out of it) while one page fades into the other.
void ControlCenter::Paint(Canvas& c)
{
    const Theme& t = c.theme;
    const float winH = winH_ = WinH(c.scale);
    const float target = HeightOf(page_);
    if (target != std::min(toH_, winH)) {
        fromH_ = std::min(shownH_, winH);
        toH_ = target;
        heightMs_ = c.now;
    }
    const float he = heightMs_ > 0 ? motion::EaseOut(float((c.now - heightMs_) / kPageMs)) : 1.f;
    shownH_ = std::min(fromH_ + (toH_ - fromH_) * he, winH);
    const float e = pageDir_ ? motion::EaseOut(float((c.now - pageMs_) / kPageMs)) : 1.f;
    if (he < 1 || e < 1)
        c.Continue();
    const bool morph = e < 1 && fromPage_ != page_ && (fromPage_ == Page::Main || page_ == Page::Main);
    const D2D1_RECT_F card = EdgeOf(bar_) == BarEdge::Top ? D2D1::RectF(0, 0, kW, shownH_) : D2D1::RectF(0, winH - shownH_, kW, winH);
    cardTop_ = card.top;

    std::vector<ShapeDip> shapes;
    if (morph) {
        shapes = BuildMorphShapes(card, e);
    } else if (page_ == Page::Main) {
        for (ShapeDip piece : MainShapes()) {
            piece.rect.top += card.top;
            piece.rect.bottom += card.top;
            shapes.push_back(piece);
        }
    } else {
        shapes.push_back({card, radius_});
    }

    // The content hangs from the glass's top, clipped to it while it changes size.
    c.Clip(card);
    if (morph) {
        // The old page goes quickly; the new one comes once it has.
        c.BeginGroup(1 - motion::EaseOut(std::min(1.f, e / 0.4f)), 1, kW / 2, card.top, -float(pageDir_) * 18 * e, card.top);
        PaintPage(c, t, fromPage_);
        c.EndGroup();
    }
    hits_.clear();
    const float in = morph ? motion::EaseOut(std::clamp((e - 0.18f) / 0.82f, 0.f, 1.f)) : e;
    c.BeginGroup(in, 1, kW / 2, card.top, float(pageDir_) * 18 * (1 - e), card.top);
    PaintPage(c, t, page_);
    c.EndGroup();
    c.Unclip();
    surface_.SetShapes(shapes, he < 1 || e < 1);

    // At rest, clicks outside the glass reach what is under it.
    surface_.SetRegion(he >= 1 && e >= 1 ? card : D2D1_RECT_F{});
}

void ControlCenter::Switch(Canvas& c, const Theme& t, D2D1_RECT_F r, bool on, float hv)
{
    const float hh = (r.bottom - r.top) / 2;
    c.Fill(r, hh, on ? t.accent : Alpha(t.text, 0.22f + 0.08f * hv));
    const float x = on ? r.right - hh : r.left + hh, cy = (r.top + r.bottom) / 2;
    c.Circle(x, cy + 0.5f, hh - 2, D2D1::ColorF(0, 0, 0, 0.12f));
    c.Circle(x, cy, hh - 2.5f, D2D1::ColorF(1, 1, 1, 0.98f));
}

void ControlCenter::PaintMainConnections(Canvas& c, const Theme& t, D2D1_RECT_F conn, float modR)
{
    auto row = [&](int i, const std::wstring& id, const wchar_t* glyph, const std::wstring& title, const std::wstring& sub, bool on, bool avail) {
        // The row's plate sits 6 DIP in from every side of the module it borders, concentric with its corners;
        // the two plates sit 4 DIP apart.
        constexpr float kInset = 6, kBetween = 4;
        const float rh = (kTopH - 2 * kInset - kBetween) / 2, y0 = conn.top + kInset + float(i) * (rh + kBetween), cy = y0 + rh / 2;
        const std::wstring page = id + L"-page";
        const D2D1_RECT_F text = D2D1::RectF(conn.left + kInset, y0, conn.right - kInset, y0 + rh);
        const float th = Hover(c, page, hover_ == page);
        if (th > 0.001f || pressed_ == page)
            c.Fill(text, std::max(modR - kInset, 4.f), pressed_ == page ? t.press : Alpha(t.hover, th));
        hits_.push_back({text, page});
        const D2D1_RECT_F disc = D2D1::RectF(conn.left + 14, cy - 18, conn.left + 50, cy + 18);
        const float dh = Hover(c, id, hover_ == id);
        c.Circle((disc.left + disc.right) / 2, cy, 18, on ? t.accent : Alpha(t.text, 0.16f + 0.08f * dh));
        const bool halo = c.halo;
        c.halo = false;
        const D2D1_COLOR_F mark = on ? D2D1::ColorF(1, 1, 1, 0.98f) : avail ? t.text : t.faint;
        if (id == L"wifi")
            c.WifiFan((disc.left + disc.right) / 2, cy, 13, 3, mark);
        else
            c.Glyph(glyph, disc, 15, mark);
        c.halo = halo;
        if (avail)
            hits_.push_back({disc, id});
        c.Text(title, D2D1::RectF(disc.right + 10, cy - 18, conn.right - 10, cy), 13.5f, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        c.Text(sub, D2D1::RectF(disc.right + 10, cy, conn.right - 10, cy + 18), 11.5f, t.subtext);
    };
    const std::wstring wifiSub = WifiTileSubtitle(status_, radiosRead_, radios_.wifiPresent, radios_.wifiOn, private_);
    const bool wifiOn = radiosRead_ ? radios_.wifiOn : status_.wifi && status_.connected;
    row(0, L"wifi", L"\uE701", L"Wi-Fi", wifiSub, wifiOn, !radiosRead_ || radios_.wifiPresent);
    const int connected = int(std::count_if(devices_.begin(), devices_.end(), [](const BluetoothDevice& d) { return d.connected; }));
    const std::wstring btSub = !radiosRead_       ? L"…" :
                               !radios_.btPresent ? Tr(L"Unavailable") :
                               !radios_.btOn      ? Tr(L"Off") :
                               connected          ? TrFormat(L"%d devices connected", connected) :
                                                    Tr(L"On");
    row(1, L"bt", L"\uE702", Tr(L"Bluetooth"), btSub, radios_.btOn, radios_.btPresent);
}

void ControlCenter::PaintMainBattery(Canvas& c, const Theme& t, D2D1_RECT_F batt, float modR)
{
    const float bh = Hover(c, L"battery", hover_ == L"battery");
    if (bh > 0.001f || pressed_ == L"battery")
        c.Fill(batt, modR, pressed_ == L"battery" ? t.press : Alpha(t.hover, bh));
    const float cx = batt.left + 32, cy = (batt.top + batt.bottom) / 2;
    if (status_.battery) {
        const int pct = std::clamp(status_.batteryPercent, 0, 100);
        const D2D1_COLOR_F level = status_.charging ? D2D1::ColorF(0x30D158) : pct <= 20 ? D2D1::ColorF(0xFF453A) : t.text;
        c.Arc(cx, cy, 15, -90, 270, Alpha(t.text, 0.18f), 4);
        c.Arc(cx, cy, 15, -90, -90 + 360.f * float(pct) / 100.f, level, 4);
        if (status_.charging)
            c.Glyph(L"\uE945", D2D1::RectF(cx - 12, cy - 12, cx + 12, cy + 12), 11, t.text);
        const std::wstring state = status_.charging            ? (pct >= 100 ? Tr(L"Fully charged") : Tr(L"Charging")) :
                                   status_.batteryMinutes >= 0 ? (status_.batteryMinutes >= 60 ? TrFormat(L"%d hours left", status_.batteryMinutes / 60) :
                                                                                                 TrFormat(L"%d minutes left", status_.batteryMinutes)) :
                                                                 std::wstring(Tr(L"On battery"));
        c.Text(std::to_wstring(pct) + L"%", D2D1::RectF(cx + 24, cy - 18, batt.right - 8, cy + 1), 15, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        c.Text(state, D2D1::RectF(cx + 24, cy + 1, batt.right - 8, cy + 18), 11.5f, t.subtext);
    } else {
        c.Circle(cx, cy, 16, Alpha(t.text, 0.16f));
        c.Glyph(L"\uE945", D2D1::RectF(cx - 14, cy - 14, cx + 14, cy + 14), 13, t.text);
        c.Text(Tr(L"Power"), D2D1::RectF(cx + 24, cy - 18, batt.right - 8, cy + 1), 13.5f, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        c.Text(Tr(L"Plugged in"), D2D1::RectF(cx + 24, cy + 1, batt.right - 8, cy + 18), 11.5f, t.subtext);
    }
    hits_.push_back({batt, L"battery"});
}

void ControlCenter::PaintMainCast(Canvas& c, const Theme& t, D2D1_RECT_F cast, float modR)
{
    const float ch = Hover(c, L"cast", hover_ == L"cast");
    if (ch > 0.001f || pressed_ == L"cast")
        c.Fill(cast, modR, pressed_ == L"cast" ? t.press : Alpha(t.hover, ch));
    const float cx = cast.left + 32, cy = (cast.top + cast.bottom) / 2;
    c.Circle(cx, cy, 16, Alpha(t.text, 0.16f));
    c.Glyph(L"\uE7F4", D2D1::RectF(cx - 14, cy - 14, cx + 14, cy + 14), 13, t.text);
    c.Text(Tr(L"Cast"), D2D1::RectF(cx + 24, cy - 18, cast.right - 8, cy + 1), 13.5f, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.Text(projecting_.empty() ? Tr(L"Wireless display") : Tr(L"Connected"), D2D1::RectF(cx + 24, cy + 1, cast.right - 8, cy + 18), 11.5f,
           projecting_.empty() ? t.subtext : t.accent);
    hits_.push_back({cast, L"cast"});
}

void ControlCenter::PaintMain(Canvas& c, const Theme& t)
{
    const float colW = (kW - kGap) / 2;
    const D2D1_RECT_F conn = D2D1::RectF(0, 0, colW, kTopH);
    const D2D1_RECT_F batt = D2D1::RectF(colW + kGap, 0, kW, (kTopH - kGap) / 2);
    const D2D1_RECT_F cast = D2D1::RectF(colW + kGap, batt.bottom + kGap, kW, kTopH);
    float y = kTopH + kGap + ModulesHeight();
    const D2D1_RECT_F bright = D2D1::RectF(0, y, kW, y + kSliderH);
    y = bright.bottom + kGap;
    const D2D1_RECT_F sound = D2D1::RectF(0, y, kW, y + kSliderH);
    const float modR = std::min(radius_, 22.f);
    if (c.panel)
        for (const ShapeDip& s : MainShapes())
            c.Fill(s.rect, s.radius, t.panel);

    PaintMainConnections(c, t, conn, modR);
    PaintMainBattery(c, t, batt, modR);
    PaintMainCast(c, t, cast, modR);
    PaintModules(c, t);

    const std::wstring brightText = CanSetBrightness() ? std::to_wstring(brightness_) + L"%" : brightness_ == -1 ? Tr(L"Not adjustable") : std::wstring();
    PaintSlider(c, t, bright, L"bright", Tr(L"Displays"), L"\uE706", std::max(brightness_, 0), false, brightText, brightTrack_, CanSetBrightness());
    const std::wstring level = !status_.audio ? Tr(L"No output device") : status_.muted ? Tr(L"Muted") : std::to_wstring(status_.volume) + L"%";
    const wchar_t* g = !status_.audio || status_.muted ? L"\uE74F" : status_.volume < 34 ? L"\uE993" : status_.volume < 67 ? L"\uE994" : L"\uE995";
    PaintSlider(c, t, sound, L"slider", Tr(L"Sound"), g, status_.audio ? status_.volume : 0, status_.muted, level, volumeTrack_);
}

void ControlCenter::PaintSlider(Canvas& c, const Theme& t, D2D1_RECT_F card, const std::wstring& id, const std::wstring& title, const std::wstring& glyph,
                                int value, bool dim, const std::wstring& right, D2D1_RECT_F& track, bool enabled)
{
    if (!enabled) { // greyed out: the title and the empty track faint, the symbol at its start, nothing to press
        c.Text(title, D2D1::RectF(card.left + kPad, card.top + 8, card.right - 130, card.top + 28), 12.5f, Alpha(t.text, 0.4f), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        c.Text(right, D2D1::RectF(card.right - 130, card.top + 8, card.right - kPad, card.top + 28), 11.5f, Alpha(t.text, 0.4f), DWRITE_FONT_WEIGHT_NORMAL, 2);
        track = D2D1::RectF(card.left + kPad, card.top + 34, card.right - kPad, card.top + 58);
        const float th = track.bottom - track.top;
        c.Fill(track, th / 2, Alpha(t.text, 0.08f));
        c.Glyph(glyph, D2D1::RectF(track.left + 2, track.top, track.left + th + 2, track.bottom), 11, Alpha(t.text, 0.35f));
        return;
    }
    c.Text(title, D2D1::RectF(card.left + kPad, card.top + 8, card.right - 130, card.top + 28), 12.5f, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    // The sound opens its page (the output device, each app) from the chevron or the title.
    const bool more = id == L"slider";
    c.Text(right, D2D1::RectF(card.right - 130, card.top + 8, card.right - kPad - (more ? 30 : 0), card.top + 28), 11.5f, t.subtext, DWRITE_FONT_WEIGHT_NORMAL,
           2);
    if (more) {
        const D2D1_RECT_F chev = D2D1::RectF(card.right - kPad - 24, card.top + 6, card.right - kPad, card.top + 30);
        const float mh = Hover(c, L"sound-page", hover_ == L"sound-page");
        c.Fill(chev, 12, pressed_ == L"sound-page" ? t.press : Alpha(t.text, 0.08f + 0.08f * mh));
        c.Glyph(L"\uE76C", chev, 9.5f, t.text);
        hits_.push_back({D2D1::RectF(card.left, card.top, card.right, card.top + 30), L"sound-page"});
    }
    // A wide pill filled from the left, as on macOS, the symbol at its start.
    track = D2D1::RectF(card.left + kPad, card.top + 34, card.right - kPad, card.top + 58);
    const float th = track.bottom - track.top, r = th / 2;
    const float hv = Hover(c, id, hover_ == id || dragging_ == id);
    c.Fill(track, r, Alpha(t.text, 0.14f + 0.04f * hv));
    const float k = float(std::clamp(value, 0, 100)) / 100.f;
    const D2D1_RECT_F fill = D2D1::RectF(track.left, track.top, track.left + th + (track.right - track.left - th) * k, track.bottom);
    c.Fill(fill, r, dim ? Alpha(t.text, 0.45f) : Alpha(t.text, 0.9f));
    const bool halo = c.halo;
    c.halo = false;
    // The symbol sits on the fill: the glass's side of the ink.
    const D2D1_COLOR_F onFill = t.light ? D2D1::ColorF(1, 1, 1, 0.97f) : D2D1::ColorF(0.07f, 0.07f, 0.09f, 0.97f);
    const D2D1_RECT_F g = D2D1::RectF(track.left + 2, track.top, track.left + th + 2, track.bottom);
    c.Glyph(glyph, g, 11, onFill);
    c.halo = halo;
    if (id == L"slider")
        hits_.push_back({D2D1::RectF(g.left - 2, track.top - 4, g.right, track.bottom + 4), L"mute"});
    hits_.push_back({D2D1::RectF(track.left - 4, track.top - 8, track.right + 4, track.bottom + 8), id});
}

float ControlCenter::PaintHeader(Canvas& c, const Theme& t, D2D1_RECT_F card, const std::wstring& title, bool on, const std::wstring& toggleId)
{
    const float cy = card.top + kHeader / 2;
    const D2D1_RECT_F back = D2D1::RectF(card.left + 12, cy - 16, card.left + 44, cy + 16);
    const float bh = Hover(c, L"back", hover_ == L"back");
    c.Fill(back, 16, pressed_ == L"back" ? t.press : Alpha(t.hover, 0.5f + 0.5f * bh));
    c.Glyph(L"\uE76B", back, 11, t.text);
    hits_.push_back({back, L"back"});
    c.Text(title, D2D1::RectF(back.right + 10, cy - 12, card.right - 120, cy + 12), 15, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.Line(card.left + kPad + 4, card.top + kHeader, card.right - kPad - 4, card.top + kHeader, t.divider);
    if (toggleId.empty())
        return card.right - kPad;
    const D2D1_RECT_F sw = D2D1::RectF(card.right - kPad - 6 - 44, cy - 13, card.right - kPad - 6, cy + 13);
    Switch(c, t, sw, on, Hover(c, toggleId, hover_ == toggleId));
    hits_.push_back({sw, toggleId});
    return sw.left;
}

void ControlCenter::PaintWifiRow(Canvas& c, const Theme& t, const WifiNetwork& n, int idx, float w, float y)
{
    const D2D1_RECT_F row = D2D1::RectF(kPad - 4, y + 2, w - kPad + 4, y + kRowH - 2);
    const std::wstring id = L"net:" + std::to_wstring(idx), disId = L"disconnect:" + std::to_wstring(idx);
    const float hv = Hover(c, id, hover_ == id || (hover_ == disId && n.connected));
    if (hv > 0.001f)
        c.Fill(row, (row.bottom - row.top) / 2, Alpha(pressed_ == id ? t.press : t.hover, hv));
    const float cy = (row.top + row.bottom) / 2;
    Bars(c, row.left + 16, cy, n.signal, n.connected ? t.accent : t.text);
    const std::wstring ssid = private_ ? L"Wi-Fi " + std::to_wstring(idx + 1) : n.ssid;
    c.Text(ssid, D2D1::RectF(row.left + 44, row.top, row.right - 110, row.bottom), 14, t.text,
           n.connected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    const float right = row.right - 12;
    if (n.secured)
        c.Glyph(L"\uE72E", D2D1::RectF(row.right - 112, row.top, row.right - 92, row.bottom), 11, t.faint);
    if (n.connected && hv > 0.05f) {
        const std::wstring label = Tr(L"Disconnect");
        const float bw = c.TextWidth(label, 12) + 24;
        const D2D1_RECT_F b = D2D1::RectF(right - bw, cy - 14, right, cy + 14);
        c.BeginGroup(hv, 1, (b.left + b.right) / 2, cy);
        c.Fill(b, 14, Alpha(hover_ == disId ? t.press : t.hover, 1.4f));
        c.Text(label, b, 12, t.text, DWRITE_FONT_WEIGHT_NORMAL, 1);
        c.EndGroup();
        hits_.push_back({b, disId});
    } else if (n.connected) {
        c.Text(Tr(L"Connected"), D2D1::RectF(right - 80, row.top, right, row.bottom), 12, t.accent, DWRITE_FONT_WEIGHT_SEMI_BOLD, 2);
    } else if (hv > 0.05f) {
        c.Text(n.profile.empty() && n.secured ? Tr(L"Enter password…") : Tr(L"Connect"), D2D1::RectF(right - 90, row.top, right, row.bottom), 12,
               Alpha(t.subtext, hv), DWRITE_FONT_WEIGHT_NORMAL, 2);
    }
    hits_.push_back({row, id});
}

void ControlCenter::PaintWifi(Canvas& c, const Theme& t, D2D1_RECT_F card)
{
    const float w = card.right - card.left;
    const float swLeft = PaintHeader(c, t, card, L"Wi-Fi", radios_.wifiOn, L"wifi");
    if (radios_.wifiOn) {
        const D2D1_RECT_F rescan = D2D1::RectF(swLeft - 42, card.top + kHeader / 2 - 15, swLeft - 12, card.top + kHeader / 2 + 15);
        const float rh = Hover(c, L"rescan", hover_ == L"rescan");
        if (rh > 0.001f)
            c.Fill(rescan, 15, Alpha(t.hover, rh));
        c.Glyph(scanning_ ? L"\uE895" : L"\uE72C", rescan, 12, t.subtext);
        hits_.push_back({rescan, L"rescan"});
    }
    float y = card.top + kHeader;
    const bool on = !radiosRead_ || radios_.wifiOn;
    if (!on || networks_.empty())
        PaintWifiEmpty(c, t, w, y);
    const int maxRows = std::clamp(int((card.bottom - y - kFooter - 6) / kRowH), 1, kMaxRows);
    for (int i = 0; on && i < maxRows && i < int(networks_.size()); ++i) {
        PaintWifiRow(c, t, networks_[size_t(i)], i, w, y);
        y += kRowH;
    }
    c.Line(kPad + 4, y + 2, w - kPad - 4, y + 2, t.divider);
    Link(c, t, L"netsettings", Tr(L"Network & Internet settings"), kPad - 4, w / 2 + 20, y, 0);
    Link(c, t, L"netlist", Tr(L"More Wi-Fi networks"), w / 2 + 28, w - kPad + 4, y, 2);
}

void ControlCenter::PaintBluetooth(Canvas& c, const Theme& t, D2D1_RECT_F card)
{
    const float w = card.right - card.left;
    PaintHeader(c, t, card, Tr(L"Bluetooth"), radios_.btOn, L"bt");
    float y = card.top + kHeader;
    auto message = [&](const wchar_t* text) {
        c.Text(text, D2D1::RectF(kPad, y, w - kPad, y + kRowH), 13, t.subtext, DWRITE_FONT_WEIGHT_NORMAL, 1);
        y += kRowH;
    };
    if (radiosRead_ && !radios_.btPresent)
        message(Tr(L"This PC has no Bluetooth"));
    else if (!radios_.btOn)
        message(Tr(L"Bluetooth is off"));
    else if (devices_.empty())
        message(devicesRead_ ? Tr(L"No paired devices") : Tr(L"Reading devices…"));
    const int maxRows = std::clamp(int((card.bottom - y - kFooter - 6) / kRowH), 1, kMaxRows);
    for (int i = 0; radios_.btOn && i < maxRows && i < int(devices_.size()); ++i) {
        const BluetoothDevice& d = devices_[size_t(i)];
        const D2D1_RECT_F row = D2D1::RectF(kPad - 4, y + 2, w - kPad + 4, y + kRowH - 2);
        const std::wstring id = L"bt:" + std::to_wstring(i);
        const float hv = Hover(c, id, hover_ == id);
        if (hv > 0.001f)
            c.Fill(row, (row.bottom - row.top) / 2, Alpha(pressed_ == id ? t.press : t.hover, hv));
        const float cy = (row.top + row.bottom) / 2;
        const D2D1_RECT_F disc = D2D1::RectF(row.left + 8, cy - 14, row.left + 36, cy + 14);
        c.Circle((disc.left + disc.right) / 2, cy, 14, d.connected ? t.accent : Alpha(t.text, 0.14f));
        const bool halo = c.halo;
        c.halo = false;
        c.Glyph(L"\uE702", disc, 12, d.connected ? D2D1::ColorF(1, 1, 1, 0.98f) : t.text);
        c.halo = halo;
        const std::wstring name = private_ ? TrFormat(L"Bluetooth device %d", i + 1) : d.name;
        c.Text(name, D2D1::RectF(disc.right + 10, row.top, row.right - 80, row.bottom), 13.5f, t.text,
               d.connected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        c.Text(d.connected ? Tr(L"Connected") : Tr(L"Not connected"), D2D1::RectF(row.right - 80, row.top, row.right - 12, row.bottom), 12,
               d.connected ? t.accent : t.faint, d.connected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, 2);
        hits_.push_back({row, id});
        y += kRowH;
    }
    c.Line(kPad + 4, y + 2, w - kPad - 4, y + 2, t.divider);
    Link(c, t, L"btsettings", Tr(L"Bluetooth & devices settings"), kPad - 4, w / 2 + 40, y, 0);
}

void ControlCenter::Link(Canvas& c, const Theme& t, const std::wstring& id, const std::wstring& label, float left, float right, float y, int align)
{
    const D2D1_RECT_F b = D2D1::RectF(left, y + 9, right, y + kFooter - 3);
    const float hv = Hover(c, id, hover_ == id);
    if (hv > 0.001f)
        c.Fill(b, (b.bottom - b.top) / 2, Alpha(t.hover, hv));
    c.Text(label, D2D1::RectF(b.left + 12, b.top, b.right - 12, b.bottom), 12.5f, t.subtext, DWRITE_FONT_WEIGHT_NORMAL, align);
    hits_.push_back({b, id});
}

void ControlCenter::PaintSoundOutputRow(Canvas& c, const Theme& t, const AudioOutput& o, int idx, float w, float y)
{
    const D2D1_RECT_F row = D2D1::RectF(kPad - 4, y + 2, w - kPad + 4, y + kRowH - 2);
    const std::wstring id = L"out:" + std::to_wstring(idx);
    const float hv = Hover(c, id, hover_ == id);
    if (hv > 0.001f)
        c.Fill(row, (row.bottom - row.top) / 2, Alpha(pressed_ == id ? t.press : t.hover, hv));
    const float cy = (row.top + row.bottom) / 2;
    const D2D1_RECT_F disc = D2D1::RectF(row.left + 8, cy - 14, row.left + 36, cy + 14);
    c.Circle((disc.left + disc.right) / 2, cy, 14, o.isDefault ? t.accent : Alpha(t.text, 0.14f));
    const wchar_t* g = o.kind == AudioOutput::Headphones ? L"\uE7F6" : o.kind == AudioOutput::Display ? L"\uE7F4" : L"\uE767";
    const bool halo = c.halo;
    c.halo = false;
    c.Glyph(g, disc, 12, o.isDefault ? D2D1::ColorF(1, 1, 1, 0.98f) : t.text);
    c.halo = halo;
    const float right = row.right - (o.isDefault ? 36 : 12);
    if (o.adapter.empty()) {
        c.Text(o.name, D2D1::RectF(disc.right + 10, row.top, right, row.bottom), 13.5f, t.text,
               o.isDefault ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    } else {
        c.Text(o.name, D2D1::RectF(disc.right + 10, cy - 17, right, cy + 1), 13, t.text,
               o.isDefault ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        c.Text(o.adapter, D2D1::RectF(disc.right + 10, cy, right, cy + 16), 11, t.subtext);
    }
    if (o.isDefault)
        c.Glyph(L"\uE73E", D2D1::RectF(row.right - 34, row.top, row.right - 10, row.bottom), 13, t.text);
    hits_.push_back({row, id});
}

void ControlCenter::PaintSoundAppRow(Canvas& c, const Theme& t, const AudioApp& a, int idx, float w, float y)
{
    const std::wstring sid = L"app:" + std::to_wstring(idx), mid = L"amute:" + std::to_wstring(idx);
    const float cy = y + kAppRowH / 2;
    // The icon mutes the app; dimmed with a struck speaker when muted.
    const D2D1_RECT_F icon = D2D1::RectF(kPad, cy - 13, kPad + 26, cy + 13);
    const float ih = Hover(c, mid, hover_ == mid);
    if (ih > 0.001f || pressed_ == mid)
        c.Fill(D2D1::RectF(icon.left - 4, icon.top - 4, icon.right + 4, icon.bottom + 4), 9, pressed_ == mid ? t.press : Alpha(t.hover, ih));
    if (!a.icon.Empty())
        c.Image(L"audio:" + a.key, a.icon, icon, a.muted ? 0.4f : 1.f);
    else
        c.Glyph(a.key == L"#system" ? L"\uE767" : L"\uE71D", icon, 14, a.muted ? t.faint : t.text);
    if (a.muted) {
        const D2D1_RECT_F badge = D2D1::RectF(icon.right - 10, icon.bottom - 10, icon.right + 4, icon.bottom + 4);
        c.Circle((badge.left + badge.right) / 2, (badge.top + badge.bottom) / 2, 7,
                 t.light ? D2D1::ColorF(1, 1, 1, 0.95f) : D2D1::ColorF(0.1f, 0.1f, 0.12f, 0.95f));
        c.Glyph(L"\uE74F", badge, 7.5f, t.text);
    }
    hits_.push_back({D2D1::RectF(icon.left - 4, icon.top - 4, icon.right + 4, icon.bottom + 4), mid});
    const float left = icon.right + 12;
    c.Text(a.name, D2D1::RectF(left, cy - 21, w - kPad - 44, cy - 3), 12.5f, a.active ? t.text : t.subtext,
           a.active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    c.Text(a.muted ? Tr(L"Muted") : std::to_wstring(a.volume) + L"%", D2D1::RectF(w - kPad - 60, cy - 21, w - kPad, cy - 3), 11, t.subtext,
           DWRITE_FONT_WEIGHT_NORMAL, 2);
    // A thin line with a knob, as on the Windows taskbar's mixer.
    const float knob = 7, ty = cy + 9;
    const D2D1_RECT_F track = D2D1::RectF(left + knob, ty - 2, w - kPad - knob, ty + 2);
    const float hv = Hover(c, sid, hover_ == sid || dragging_ == sid);
    c.Fill(track, 2, Alpha(t.text, 0.18f + 0.04f * hv));
    const float k = float(std::clamp(a.volume, 0, 100)) / 100.f, kx = track.left + (track.right - track.left) * k;
    c.Fill(D2D1::RectF(track.left, track.top, kx, track.bottom), 2, a.muted ? Alpha(t.text, 0.4f) : Alpha(t.text, 0.85f));
    const bool halo = c.halo;
    c.halo = false;
    c.Circle(kx, ty + 0.6f, knob + 0.5f + hv, D2D1::ColorF(0, 0, 0, 0.16f));
    c.Circle(kx, ty, knob + hv, D2D1::ColorF(1, 1, 1, 0.98f));
    c.halo = halo;
    appTracks_[size_t(idx)] = track;
    hits_.push_back({D2D1::RectF(track.left - 4, track.top - 8, track.right + 4, track.bottom + 8), sid});
}

// The sound output, after the Windows taskbar's: the playback devices (a
// tick on the default one) and below, the volume mixer, one slider per app.
void ControlCenter::PaintSound(Canvas& c, const Theme& t, D2D1_RECT_F card)
{
    const float w = card.right - card.left;
    PaintHeader(c, t, card, Tr(L"Sound output"), false, L"");
    float y = card.top + kHeader;
    auto message = [&](const wchar_t* text, float h) {
        c.Text(text, D2D1::RectF(kPad, y, w - kPad, y + h), 13, t.subtext, DWRITE_FONT_WEIGHT_NORMAL, 1);
        y += h;
    };
    if (outputs_.empty())
        message(audioRead_ ? Tr(L"No output device") : Tr(L"Reading…"), kRowH);
    for (int i = 0; i < kMaxOutputs && i < int(outputs_.size()); ++i) {
        PaintSoundOutputRow(c, t, outputs_[size_t(i)], i, w, y);
        y += kRowH;
    }

    // The volume mixer.
    c.Line(kPad + 4, y + 2, w - kPad - 4, y + 2, t.divider);
    c.Text(Tr(L"Volume mixer"), D2D1::RectF(kPad, y + 8, w - kPad, y + kSection), 12, t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    y += kSection;
    appTracks_.assign(apps_.size(), D2D1_RECT_F{});
    if (apps_.empty())
        message(audioRead_ ? Tr(L"No apps are playing sound") : Tr(L"Reading…"), kAppRowH);
    const int maxApps = std::clamp(int((card.bottom - y - kFooter - 6) / kAppRowH), 1, kMaxApps);
    for (int i = 0; i < maxApps && i < int(apps_.size()); ++i) {
        PaintSoundAppRow(c, t, apps_[size_t(i)], i, w, y);
        y += kAppRowH;
    }
    c.Line(kPad + 4, y + 2, w - kPad - 4, y + 2, t.divider);
    Link(c, t, L"soundsettings", Tr(L"More sound settings"), kPad - 4, w / 2 + 10, y, 0);
    Link(c, t, L"mixer", Tr(L"Volume mixer settings"), w / 2 + 18, w - kPad + 4, y, 2);
}

void ControlCenter::PaintCastDisplayRow(Canvas& c, const Theme& t, const WirelessDisplay& d, int idx, float w, float y)
{
    const D2D1_RECT_F row = D2D1::RectF(kPad - 4, y + 2, w - kPad + 4, y + kRowH - 2);
    const std::wstring id = L"disp:" + std::to_wstring(idx);
    const bool on = d.id == projecting_, busy = d.id == connecting_;
    const float hv = Hover(c, id, hover_ == id || (hover_ == L"castoff" && on));
    if (hv > 0.001f)
        c.Fill(row, (row.bottom - row.top) / 2, Alpha(pressed_ == id ? t.press : t.hover, hv));
    const float cy = (row.top + row.bottom) / 2;
    const D2D1_RECT_F disc = D2D1::RectF(row.left + 8, cy - 14, row.left + 36, cy + 14);
    c.Circle((disc.left + disc.right) / 2, cy, 14, on ? t.accent : Alpha(t.text, 0.14f));
    const bool halo = c.halo;
    c.halo = false;
    c.Glyph(L"\uE7F4", disc, 12, on ? D2D1::ColorF(1, 1, 1, 0.98f) : t.text);
    c.halo = halo;
    const std::wstring name = private_ ? TrFormat(L"Wireless display %d", idx + 1) : d.name;
    c.Text(name, D2D1::RectF(disc.right + 10, row.top, row.right - 96, row.bottom), 13.5f, t.text,
           on ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    const float r = row.right - 12;
    if (busy) {
        c.Text(Tr(L"Connecting…"), D2D1::RectF(r - 90, row.top, r, row.bottom), 12, t.subtext, DWRITE_FONT_WEIGHT_NORMAL, 2);
    } else if (on && hv > 0.05f) {
        const std::wstring label = Tr(L"Disconnect");
        const float bw = c.TextWidth(label, 12) + 24;
        const D2D1_RECT_F b = D2D1::RectF(r - bw, cy - 14, r, cy + 14);
        c.BeginGroup(hv, 1, (b.left + b.right) / 2, cy);
        c.Fill(b, 14, Alpha(hover_ == L"castoff" ? t.press : t.hover, 1.4f));
        c.Text(label, b, 12, t.text, DWRITE_FONT_WEIGHT_NORMAL, 1);
        c.EndGroup();
        hits_.push_back({b, L"castoff"});
    } else if (on) {
        c.Text(Tr(L"Connected"), D2D1::RectF(r - 80, row.top, r, row.bottom), 12, t.accent, DWRITE_FONT_WEIGHT_SEMI_BOLD, 2);
    } else if (hv > 0.05f) {
        c.Text(Tr(L"Connect"), D2D1::RectF(r - 80, row.top, r, row.bottom), 12, Alpha(t.subtext, hv), DWRITE_FONT_WEIGHT_NORMAL, 2);
    }
    hits_.push_back({row, id});
}

void ControlCenter::PaintCastModes(Canvas& c, const Theme& t, float w, float y)
{
    c.Line(kPad + 4, y + 2, w - kPad - 4, y + 2, t.divider);
    c.Text(Tr(L"Projection mode"), D2D1::RectF(kPad, y + 8, w - kPad, y + kSection), 12, t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (monitors_ < 2)
        c.Text(Tr(L"Only one display is connected"), D2D1::RectF(kPad, y + 8, w - kPad, y + kSection), 11, t.faint, DWRITE_FONT_WEIGHT_NORMAL, 2);
    y += kSection;
    const ProjectionMode modes[4] = {ProjectionMode::Internal, ProjectionMode::Clone, ProjectionMode::Extend, ProjectionMode::External};
    const wchar_t* labels[4] = {Tr(L"PC screen only"), Tr(L"Duplicate"), Tr(L"Extend"), Tr(L"Second screen only")};
    const float tg = 8, tw = (w - 2 * kPad - 3 * tg) / 4;
    for (int i = 0; i < 4; ++i) {
        const D2D1_RECT_F tile = D2D1::RectF(kPad + float(i) * (tw + tg), y + 4, kPad + float(i) * (tw + tg) + tw, y + kModeH - 8);
        const bool cur = modes[i] == mode_, usable = monitors_ >= 2 || cur;
        const std::wstring id = L"mode:" + std::to_wstring(i);
        const float hv = usable ? Hover(c, id, hover_ == id) : 0.f;
        c.Fill(tile, 14, cur ? t.accent : pressed_ == id ? t.press : Alpha(t.text, 0.07f + 0.07f * hv));
        const D2D1_COLOR_F ink = cur ? D2D1::ColorF(1, 1, 1, 0.98f) : usable ? t.text : Alpha(t.text, 0.42f);
        const bool halo = c.halo;
        c.halo = !cur;
        ModePicture(c, modes[i], (tile.left + tile.right) / 2, tile.top + 22, ink);
        c.Text(labels[i], D2D1::RectF(tile.left + 2, tile.top + 36, tile.right - 2, tile.bottom - 4), 11, ink,
               cur ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, 1);
        c.halo = halo;
        if (usable && !cur)
            hits_.push_back({tile, id});
    }
}

// Casting, after the Windows taskbar's cast panel: the wireless displays in
// range, then Win+P's projection modes.
void ControlCenter::PaintCast(Canvas& c, const Theme& t, D2D1_RECT_F card)
{
    const float w = card.right - card.left;
    const float right = PaintHeader(c, t, card, Tr(L"Cast"), false, L"");
    const bool searching = finder_.Searching();
    const D2D1_RECT_F rescan = D2D1::RectF(right - 30, card.top + kHeader / 2 - 15, right, card.top + kHeader / 2 + 15);
    const float rh = Hover(c, L"castscan", hover_ == L"castscan");
    if (rh > 0.001f)
        c.Fill(rescan, 15, Alpha(t.hover, rh));
    c.Glyph(searching ? L"\uE895" : L"\uE72C", rescan, 12, t.subtext);
    hits_.push_back({rescan, L"castscan"});

    float y = card.top + kHeader;
    if (displays_.empty()) {
        c.Text(searching ? Tr(L"Searching for wireless displays…") : Tr(L"No wireless displays found"), D2D1::RectF(kPad, y, w - kPad, y + kRowH), 13,
               t.subtext, DWRITE_FONT_WEIGHT_NORMAL, 1);
        y += kRowH;
    }
    const int maxDisplays = std::clamp(int((card.bottom - y - kSection - kModeH - kFooter - 6) / kRowH), 1, kMaxDisplays);
    for (int i = 0; i < maxDisplays && i < int(displays_.size()); ++i) {
        PaintCastDisplayRow(c, t, displays_[size_t(i)], i, w, y);
        y += kRowH;
    }
    PaintCastModes(c, t, w, y);
    y += kSection + kModeH;
    c.Line(kPad + 4, y + 2, w - kPad - 4, y + 2, t.divider);
    Link(c, t, L"castproject", Tr(L"Project to this PC"), kPad - 4, w / 2 + 10, y, 0);
    Link(c, t, L"castpanel", Tr(L"System cast panel"), w / 2 + 18, w - kPad + 4, y, 2);
}

} // namespace atrium
