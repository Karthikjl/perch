// Settings window: 654 x 556 DIPs, a 192-DIP
// sidebar of coloured section icons, cards of rows on the right). See settingsui.h.
// Parts Perch doesn't do yet are drawn dimmed with "Coming soon" and do nothing.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <cwchar>
#include <cmath>
#include "settingsui.h"
#include "mascot.h"

#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#endif

namespace {

const GUID kIID_ID2D1Factory = {0x06152247, 0x6F50, 0x465A, {0x92, 0x45, 0x11, 0x8B, 0xFD, 0x3B, 0x60, 0x07}};
const GUID kIID_IDWriteFactory = {0xB859EE5A, 0xD838, 0x4B5B, {0xA2, 0xE8, 0x1A, 0xDC, 0x7D, 0x93, 0xDB, 0x48}};
const GUID kCLSID_WIC = {0xCACAF262, 0x9370, 0x4615, {0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A}};
const GUID kIID_IWICImagingFactory = {0xEC5EC8A9, 0xC395, 0x4314, {0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70}};
const GUID kWICPixelFormat32bppPBGRA = {0x6FDDC324, 0x4E03, 0x4BFE, {0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x10}};
const wchar_t* kClass = L"PerchSettings";
const wchar_t* kVersion = L"0.1.0";

// ---- layout (DIPs) ----
const float kW = 654.4f, kH = 556, kSideW = 192;
const float kCardL = 213.6f, kCardR = 632, kPadIn = 14.4f;
const float kRow1 = 40, kRow2 = 48; // one-line / two-line rows

// ---- colours ----
D2D1_COLOR_F Hex(int v, float a = 1) { return D2D1::ColorF(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, a); }
const int cSide = 0x151517, cContent = 0x1C1C1E, cSel = 0x2A2A2C, cCard = 0x28282A, cDivider = 0x373739, cLabel = 0x8F8F8F,
          cSub = 0x8E8E93, cTrack = 0x3B3B3D, cSegSel = 0x636366, cOn = 0x30C759, cOff = 0x48484A, cBlue = 0x0A84FF,
          cButton = 0x1F5FBF, cStep = 0x3A3A3C;

struct SectionInfo { const wchar_t* name; const wchar_t* glyph; int color; bool soon; };
const SectionInfo kSections[SEC_COUNT] = {
    {L"General", L"\xE713", 0x8F8F94, false},    {L"Display", L"\xE7F4", 0x5E5CE5, false},
    {L"Pages", L"\xE8FD", 0x33ADE5, false},      {L"Appearance", L"\xE793", 0x424247, false},
    {L"Music and lyrics", L"\xEC4F", 0xFF385E, false}, {L"Widgets", L"\xF0E2", 0x0A85FF, true},
    {L"Ask Perch", L"\xE8BD", 0x6B54FF, false},   {L"To-do", L"\xE73E", 0x30C759, false},
    {L"Pomodoro", L"\xE916", 0x7D66FA, false},   {L"Water", L"\xEB42", 0x0A85FF, false},
    {L"Shelf", L"\xE7B8", 0xFFB233, false},      {L"HUD & popups", L"\xEA8F", 0x30ABDB, false},
    {L"About", L"\xE946", 0x636369, false}};

// Control ids (a hit is id + arg).
enum Id {
    I_NONE = 0, I_CLOSE, I_SECTION, // arg = section
    G_AUTOSTART, G_SIZE, G_MINIMAL, G_MASCOT, G_CLOCK, G_SOUNDS, G_HIDEFS,
    D_RING, // arg = card
    P_UP, P_DOWN, P_TOGGLE, // arg = page kind
    A_STYLE, A_GLASS, A_TINT, A_OPEN, A_PAGEICONS, A_SPEED,
    T_START, T_END, T_SNOOZE, T_WEEK, T_VIEW, T_PASTE, T_REFRESH, T_REMOVE,
    O_FOCUS, O_SHORT, O_LONG,
    S_EMPTY,
    H_VOLUME, H_BRIGHT, H_CAPS, H_LANG, H_BATTERY, H_WIFI, H_BT,
    B_FILE, B_RESET,
    G_PETSTYLE, // arg = mascot style
    W_REMIND, W_GOAL, W_CUP, W_FROM, W_UNTIL,
    C_HISTORY, C_MAX, C_REMEMBER,
    T_REMIND,
    K_PASTE, K_REMOVE, K_MODEL, K_HOTKEY, K_HOTKEY_OFF,
    M_LYRICS, M_EARLIER,
    I_SOON, // a Coming soon control (never hit)
};

// ---- state ----
HWND g_wnd;
SettingsUIHost g_host;
Settings g_s;
int g_sec = SEC_GENERAL;
float g_scroll = 0, g_contentH = 0;
int g_hotId = -1, g_hotArg = 0;
float g_scale = 1;
bool g_paint = false; // Build() draws (true) or only lays out hits (false)
bool g_recordKey = false; // Ask Perch > Shortcut: waiting for the keys
struct Hit { D2D1_RECT_F r; int id, arg; };
std::vector<Hit> g_hits;

ID2D1Factory* g_d2d;
IDWriteFactory* g_dw;
ID2D1HwndRenderTarget* g_rt;
ID2D1SolidColorBrush* g_br;
ID2D1Bitmap* g_logo;
IDWriteTextFormat *g_fStep, *g_fCaps, *g_fSideIcon, *g_fTitle, *g_fItem, *g_fRow, *g_fSub, *g_fLabel, *g_fSeg, *g_fIcon, *g_fIconBig, *g_fBadge, *g_fCardT, *g_fCenter,
    *g_fBig, *g_fRight;

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

IDWriteTextFormat* Format(const wchar_t* family, DWRITE_FONT_WEIGHT w, float size, DWRITE_TEXT_ALIGNMENT align) {
    IDWriteTextFormat* f = nullptr;
    g_dw->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    if (f) {
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return f;
}
float TextW(const wchar_t* s, IDWriteTextFormat* f) {
    IDWriteTextLayout* l = nullptr;
    float w = 0;
    if (SUCCEEDED(g_dw->CreateTextLayout(s, (UINT32)wcslen(s), f, 1000, 100, &l))) {
        DWRITE_TEXT_METRICS m;
        l->GetMetrics(&m);
        w = m.widthIncludingTrailingWhitespace;
        l->Release();
    }
    return w;
}
void Text(const wchar_t* s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c) {
    if (!g_paint) return;
    g_br->SetColor(c);
    g_rt->DrawText(s, (UINT32)wcslen(s), f, r, g_br);
}
void Fill(D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
    if (!g_paint) return;
    g_br->SetColor(c);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), g_br);
}
void Circle(D2D1_POINT_2F p, float rad, D2D1_COLOR_F c) {
    if (!g_paint) return;
    g_br->SetColor(c);
    g_rt->FillEllipse(D2D1::Ellipse(p, rad, rad), g_br);
}
void AddHit(D2D1_RECT_F r, int id, int arg = 0) { if (id != I_SOON) g_hits.push_back({r, id, arg}); }
bool Hot(int id, int arg = 0) { return g_hotId == id && g_hotArg == arg; }
D2D1_COLOR_F Dim(D2D1_COLOR_F c, bool soon) { if (soon) c.a *= 0.42f; return c; }

// ---- values ----
bool GetBool(int id) {
    switch (id) {
    case G_AUTOSTART: return g_host.getAutostart && g_host.getAutostart();
    case G_MINIMAL: return g_s.minimal;
    case G_MASCOT: return g_s.mascot;
    case G_CLOCK: return g_s.clock;
    case G_SOUNDS: return g_s.sounds;
    case G_HIDEFS: return g_s.hideFullscreen;
    case A_GLASS: return g_s.glass;
    case A_PAGEICONS: return g_s.pageIcons;
    case T_START: return g_s.todoStart;
    case T_END: return g_s.todoEnd;
    case S_EMPTY: return g_s.shelfEmpty;
    case H_VOLUME: return g_s.popVolume;
    case H_BRIGHT: return g_s.popBrightness;
    case H_CAPS: return g_s.popCaps;
    case H_LANG: return g_s.popLanguage;
    case H_BATTERY: return g_s.popBattery;
    case H_WIFI: return g_s.popWifi;
    case H_BT: return g_s.popBluetooth;
    case W_REMIND: return g_s.waterRemind;
    case C_HISTORY: return g_s.clipHistory;
    case M_LYRICS: return g_s.lyrics;
    case C_REMEMBER: return g_s.clipRemember;
    }
    return false;
}
void FlipBool(int id) {
    switch (id) {
    case G_AUTOSTART: if (g_host.setAutostart) g_host.setAutostart(!GetBool(id)); return;
    case G_MINIMAL: g_s.minimal = !g_s.minimal; break;
    case G_MASCOT: g_s.mascot = !g_s.mascot; break;
    case G_CLOCK: g_s.clock = !g_s.clock; break;
    case G_SOUNDS: g_s.sounds = !g_s.sounds; break;
    case G_HIDEFS: g_s.hideFullscreen = !g_s.hideFullscreen; break;
    case A_GLASS: g_s.glass = !g_s.glass; break;
    case A_PAGEICONS: g_s.pageIcons = !g_s.pageIcons; break;
    case T_START: g_s.todoStart = !g_s.todoStart; break;
    case T_END: g_s.todoEnd = !g_s.todoEnd; break;
    case S_EMPTY: g_s.shelfEmpty = !g_s.shelfEmpty; break;
    case H_VOLUME: g_s.popVolume = !g_s.popVolume; break;
    case H_BRIGHT: g_s.popBrightness = !g_s.popBrightness; break;
    case H_CAPS: g_s.popCaps = !g_s.popCaps; break;
    case H_LANG: g_s.popLanguage = !g_s.popLanguage; break;
    case H_BATTERY: g_s.popBattery = !g_s.popBattery; break;
    case H_WIFI: g_s.popWifi = !g_s.popWifi; break;
    case H_BT: g_s.popBluetooth = !g_s.popBluetooth; break;
    case W_REMIND: g_s.waterRemind = !g_s.waterRemind; break;
    case C_HISTORY: g_s.clipHistory = !g_s.clipHistory; break;
    case M_LYRICS: g_s.lyrics = !g_s.lyrics; break;
    case C_REMEMBER: g_s.clipRemember = !g_s.clipRemember; break;
    default: return;
    }
    g_host.commit(g_s);
}
int GetChoice(int id) {
    switch (id) {
    case G_SIZE: return g_s.size;
    case A_OPEN: return g_s.openClick ? 1 : 0;
    case A_SPEED: return g_s.fastAnim ? 1 : 0;
    case T_SNOOZE: return g_s.snooze == 5 ? 0 : g_s.snooze == 15 ? 2 : 1;
    case T_WEEK: return g_s.weekMonday ? 1 : 0;
    case T_VIEW: return g_s.calWeek ? 1 : 0;
    case D_RING: return g_s.ring == RING_BATTERY ? 0 : g_s.ring == RING_WATER ? 1 : g_s.ring == RING_TASKS ? 2 : 3;
    case W_GOAL: return (g_s.waterGoal - 1500) / 500;
    case M_EARLIER: return g_s.lyricsEarlier >= 1000 ? 3 : g_s.lyricsEarlier >= 600 ? 2 : g_s.lyricsEarlier >= 300 ? 1 : 0;
    case T_REMIND: { static const int m[] = {-1, 0, 5, 10, 15, 30}; for (int i = 0; i < 6; i++) if (m[i] == g_s.calRemind) return i; return 3; }
    case A_STYLE: return g_s.style;
    case G_PETSTYLE: return g_s.mascotStyle;
    }
    return 0;
}
void SetChoice(int id, int v) {
    switch (id) {
    case G_PETSTYLE: g_s.mascotStyle = v; break;
    case G_SIZE: g_s.size = v; break;
    case A_OPEN: g_s.openClick = v == 1; break;
    case A_SPEED: g_s.fastAnim = v == 1; break;
    case T_SNOOZE: g_s.snooze = v == 0 ? 5 : v == 2 ? 15 : 10; break;
    case T_WEEK: g_s.weekMonday = v == 1; break;
    case T_VIEW: g_s.calWeek = v == 1; break;
    case D_RING: g_s.ring = v == 0 ? RING_BATTERY : v == 1 ? RING_WATER : v == 2 ? RING_TASKS : RING_NONE; break;
    case W_GOAL: g_s.waterGoal = 1500 + 500 * v; break;
    case M_EARLIER: { static const int ms[] = {0, 300, 600, 1000}; g_s.lyricsEarlier = ms[v < 0 ? 0 : v > 3 ? 3 : v]; break; }
    case T_REMIND: { static const int m[] = {-1, 0, 5, 10, 15, 30}; g_s.calRemind = m[v < 0 ? 0 : v > 5 ? 5 : v]; break; }
    case A_STYLE: g_s.style = v; break;
    default: return;
    }
    g_host.commit(g_s);
}
void Step(int id, int dir) {
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    switch (id) {
    case A_TINT: g_s.glassTint = clamp(g_s.glassTint + 10 * dir, 0, 100); break;
    case O_FOCUS: g_s.pomoFocus = clamp(g_s.pomoFocus + 5 * dir, 5, 120); break;
    case O_SHORT: g_s.pomoShort = clamp(g_s.pomoShort + dir, 1, 60); break;
    case O_LONG: g_s.pomoLong = clamp(g_s.pomoLong + 5 * dir, 5, 60); break;
    case W_CUP: g_s.waterCup = clamp(g_s.waterCup + 50 * dir, 100, 1000); break;
    case C_MAX: g_s.clipMax = clamp(g_s.clipMax + 5 * dir, 5, 50); break;
    case K_MODEL: { // step through the models the key may use
        std::vector<std::wstring> m;
        if (g_host.askModels) g_host.askModels(m);
        if (m.empty()) return;
        int i = -1;
        for (int k = 0; k < (int)m.size(); k++) if (m[k] == g_s.askModel) i = k;
        i = i < 0 ? 0 : (i + dir + (int)m.size()) % (int)m.size();
        g_s.askModel = m[i];
        break;
    }
    case W_FROM: g_s.waterFrom = clamp(g_s.waterFrom + 30 * dir, 0, g_s.waterUntil - 60); break;
    case W_UNTIL: g_s.waterUntil = clamp(g_s.waterUntil + 30 * dir, g_s.waterFrom + 60, 23 * 60 + 30); break;
    default: return;
    }
    g_host.commit(g_s);
}
std::wstring StepText(int id) {
    wchar_t b[32] = L"";
    switch (id) {
    case A_TINT: swprintf(b, 32, L"%d%%", g_s.glassTint); break;
    case O_FOCUS: swprintf(b, 32, L"%d min", g_s.pomoFocus); break;
    case O_SHORT: swprintf(b, 32, L"%d min", g_s.pomoShort); break;
    case O_LONG: swprintf(b, 32, L"%d min", g_s.pomoLong); break;
    case W_CUP: swprintf(b, 32, L"%d ml", g_s.waterCup); break;
    case C_MAX: swprintf(b, 32, L"%d items", g_s.clipMax); break;
    case K_MODEL: return g_s.askModel;
    case W_FROM: swprintf(b, 32, L"%02d:%02d", g_s.waterFrom / 60, g_s.waterFrom % 60); break;
    case W_UNTIL: swprintf(b, 32, L"%02d:%02d", g_s.waterUntil / 60, g_s.waterUntil % 60); break;
    }
    return b;
}

// ---- controls ----
void Toggle(float right, float cy, int id, bool soon, bool on) {
    D2D1_RECT_F r = D2D1::RectF(right - 37.6f, cy - 11.2f, right, cy + 11.2f);
    Fill(r, 11.2f, Dim(Hex(on ? cOn : cOff), soon));
    Circle(D2D1::Point2F(on ? r.right - 11.2f : r.left + 11.2f, cy), 9.2f, Dim(Hex(0xFFFFFF), soon));
    AddHit(D2D1::RectF(r.left - 6, r.top - 6, r.right + 4, r.bottom + 6), soon ? I_SOON : id);
}
void Segments(D2D1_RECT_F r, const std::vector<std::wstring>& opts, int id, int sel, bool soon, bool even) {
    Fill(r, 6, Dim(Hex(cTrack), soon));
    float total = 0;
    for (const std::wstring& o : opts) total += TextW(o.c_str(), g_fSeg) + 22;
    float x = r.left + 2, extra = even ? 0 : (r.right - r.left - 4 - total) / opts.size();
    for (size_t i = 0; i < opts.size(); i++) {
        float w = even ? (r.right - r.left - 4) / opts.size() : TextW(opts[i].c_str(), g_fSeg) + 22 + extra;
        D2D1_RECT_F s = D2D1::RectF(x, r.top + 2, x + w, r.bottom - 2);
        if ((int)i == sel) Fill(s, 5, Dim(Hex(cSegSel), soon));
        else if (Hot(id, (int)i)) Fill(s, 5, Hex(0x4A4A4C));
        Text(opts[i].c_str(), g_fSeg, s, Dim((int)i == sel ? Hex(0xFFFFFF) : Hex(0xB0B0B4), soon));
        AddHit(s, soon ? I_SOON : id, (int)i);
        x += w;
    }
}
float SegmentsWidth(const std::vector<std::wstring>& opts) {
    float total = 4;
    for (const std::wstring& o : opts) total += TextW(o.c_str(), g_fSeg) + 22;
    return total;
}
float Stepper(float right, float cy, int id, const std::wstring& value, bool soon) { // returns its left edge
    float span = std::max(95.6f, TextW(value.c_str(), g_fCenter) + 40); // wide enough for a long value (a model's name)
    D2D1_POINT_2F plus = D2D1::Point2F(right - 10.4f, cy), minus = D2D1::Point2F(right - 10.4f - span, cy);
    for (int dir : {-1, 1}) {
        D2D1_POINT_2F c = dir < 0 ? minus : plus;
        Circle(c, 10.4f, Dim(Hex(Hot(id, dir) ? 0x4A4A4C : cStep), soon));
        Text(dir < 0 ? L"\x2212" : L"+", g_fStep, D2D1::RectF(c.x - 10, c.y - 11, c.x + 10, c.y + 9), Dim(Hex(0xFFFFFF), soon));
        AddHit(D2D1::RectF(c.x - 12, c.y - 12, c.x + 12, c.y + 12), soon ? I_SOON : id, dir);
    }
    Text(value.c_str(), g_fCenter, D2D1::RectF(minus.x + 12, cy - 10, plus.x - 12, cy + 10), Dim(Hex(0xFFFFFF), soon));
    return minus.x - 10.4f;
}
void Button(D2D1_RECT_F r, const wchar_t* label, int id, bool soon, int color = cButton) {
    Fill(r, (r.bottom - r.top) / 2, Dim(Hex(Hot(id) ? 0x2A73DB : color), soon));
    Text(label, g_fSeg, r, Dim(Hex(0xFFFFFF), soon));
    AddHit(r, soon ? I_SOON : id);
}
void SoonBadge(float right, float cy) { // the little "Coming soon" tag for rows Perch doesn't do yet
    float w = TextW(L"Coming soon", g_fBadge) + 14;
    Fill(D2D1::RectF(right - w, cy - 8.8f, right, cy + 8.8f), 8.8f, Hex(0x34343A));
    Text(L"Coming soon", g_fBadge, D2D1::RectF(right - w, cy - 8.8f, right, cy + 8.8f), Hex(0xC8C8CE));
}

// One row of a card. What sits on the right depends on the kind.
enum RowKind { RK_TOGGLE, RK_SEG, RK_SEGWIDE, RK_STEP, RK_TEXT, RK_BUTTONS, RK_LABEL };
struct Row {
    RowKind kind;
    const wchar_t* title;
    const wchar_t* sub;
    int id;
    bool soon;
    std::vector<std::wstring> opts; // segments, or right-hand text / button labels
    std::vector<int> ids;           // button ids
};
float RowHeight(const Row& r) {
    if (r.kind == RK_SEGWIDE) return 83.2f;
    if (r.kind == RK_LABEL) return 32;
    return r.sub ? kRow2 : kRow1;
}
float g_y; // layout cursor (content coordinates, before scrolling)
void Label(const wchar_t* s) { // the label sits 19.2 below the card above and 9.6 above its own card
    g_y += 19.2f;
    Text(s, g_fLabel, D2D1::RectF(kCardL + 6.4f, g_y - 8, kCardR, g_y + 8), Hex(cLabel));
    g_y += 9.6f;
}
void Card(const std::vector<Row>& rows) {
    float h = 0;
    for (const Row& r : rows) h += RowHeight(r);
    Fill(D2D1::RectF(kCardL, g_y, kCardR, g_y + h), 8, Hex(cCard));
    float y = g_y;
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        float rh = RowHeight(r), right = kCardR - kPadIn;
        if (i > 0 && g_paint) Fill(D2D1::RectF(kCardL + kPadIn, y, kCardR, y + 1), 0, Hex(cDivider));
        D2D1_COLOR_F tc = Dim(Hex(0xFFFFFF), r.soon), sc = Dim(Hex(cSub), r.soon);
        float ty = r.kind == RK_SEGWIDE ? y + 16 : r.sub ? y + 15.6f : y + rh / 2;
        IDWriteTextFormat* tf = r.kind == RK_LABEL ? g_fSub : g_fRow;
        Text(r.title, tf, D2D1::RectF(kCardL + kPadIn, ty - 10, right - 150, ty + 10), r.kind == RK_LABEL ? sc : tc);
        float cy = r.kind == RK_SEGWIDE ? ty : y + rh / 2;
        float badgeRight = right;
        switch (r.kind) {
        case RK_TOGGLE: Toggle(right, cy, r.id, r.soon, !r.soon && GetBool(r.id)); badgeRight = right - 48; break;
        case RK_SEG: {
            float w = SegmentsWidth(r.opts);
            Segments(D2D1::RectF(right - w, cy - 10.8f, right, cy + 10.8f), r.opts, r.id, r.soon ? 0 : GetChoice(r.id), r.soon, false);
            badgeRight = right - w - 10;
            break;
        }
        case RK_SEGWIDE:
            Segments(D2D1::RectF(kCardL + kPadIn, y + 50.4f, right, y + 72), r.opts, r.id, r.soon ? 0 : GetChoice(r.id), r.soon, true);
            break;
        case RK_STEP: badgeRight = Stepper(right, cy, r.id, r.soon ? (r.opts.empty() ? L"" : r.opts[0]) : StepText(r.id), r.soon) - 12; break;
        case RK_TEXT:
            if (!r.opts.empty()) Text(r.opts[0].c_str(), g_fRight, D2D1::RectF(right - 260, cy - 10, right, cy + 10), Dim(Hex(0xB0B0B4), r.soon));
            badgeRight = right - (r.opts.empty() ? 0 : TextW(r.opts[0].c_str(), g_fRight) + 12);
            break;
        case RK_BUTTONS: {
            float x = right;
            for (size_t k = 0; k < r.opts.size(); k++) {
                float w = TextW(r.opts[k].c_str(), g_fSeg) + 32;
                Button(D2D1::RectF(x - w, cy - 12.8f, x, cy + 12.8f), r.opts[k].c_str(), r.ids[k], r.soon, k == 0 ? cButton : 0x3A3A3C);
                x -= w + 8;
            }
            badgeRight = x - 4;
            break;
        }
        case RK_LABEL: break;
        }
        if (r.sub && r.kind != RK_LABEL) { // the subtitle stops short of the controls on its right
            D2D1_RECT_F sr = D2D1::RectF(kCardL + kPadIn, ty + 6, r.kind == RK_SEGWIDE || r.kind == RK_TOGGLE ? kCardR - 60 : std::max(kCardL + kPadIn + 40, badgeRight - 6), ty + 26);
            if (g_paint) g_rt->PushAxisAlignedClip(sr, D2D1_ANTIALIAS_MODE_ALIASED);
            Text(r.sub, g_fSub, sr, sc);
            if (g_paint) g_rt->PopAxisAlignedClip();
        }
        if (r.soon && r.kind != RK_LABEL) SoonBadge(badgeRight, r.kind == RK_SEGWIDE ? ty : cy);
        y += rh;
    }
    g_y += h;
}
void Header(const wchar_t* title, bool soon) {
    Text(title, g_fTitle, D2D1::RectF(kCardL + 0.8f, 18, kCardR, 46), Hex(0xFFFFFF));
    if (soon) { // a gradient pill at the top right of a section that isn't there yet
        float w = TextW(L"Coming soon", g_fSeg) + 28;
        D2D1_RECT_F r = D2D1::RectF(kCardR - w, 18.4f, kCardR, 41.6f);
        if (g_paint) {
            ID2D1GradientStopCollection* stops = nullptr;
            ID2D1LinearGradientBrush* lg = nullptr;
            D2D1_GRADIENT_STOP st[2] = {{0, Hex(0x14B8F5)}, {1, Hex(0x8C52F5)}};
            if (SUCCEEDED(g_rt->CreateGradientStopCollection(st, 2, &stops)) &&
                SUCCEEDED(g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, 0), D2D1::Point2F(r.right, 0)), stops, &lg)))
                g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 11.6f, 11.6f), lg);
            SafeRelease(lg);
            SafeRelease(stops);
        }
        Text(L"Coming soon", g_fSeg, r, Hex(0xFFFFFF));
    }
    g_y = 53.6f;
}

// Mini closed notch for the Display and Appearance cards: ring + "10:08".
void MiniPill(float cx, float top, int ring, bool floating) {
    D2D1_RECT_F p = D2D1::RectF(cx - 36, top + (floating ? 8 : 0), cx + 36, top + (floating ? 8 : 0) + 19);
    Fill(floating ? p : D2D1::RectF(p.left, p.top - 8, p.right, p.bottom), floating ? 9.5f : 7, Hex(0x000000));
    if (!g_paint) return;
    D2D1_POINT_2F rc = D2D1::Point2F(p.left + 11, (p.top + p.bottom) / 2);
    if (ring >= 0) {
        int col = ring == 1 ? 0x4CADFF : 0x30C759;
        g_br->SetColor(Hex(col, 0.3f));
        g_rt->DrawEllipse(D2D1::Ellipse(rc, 4.6f, 4.6f), g_br, 1.6f);
        g_br->SetColor(Hex(col));
        g_rt->DrawEllipse(D2D1::Ellipse(rc, 4.6f, 4.6f), g_br, 1.6f);
    }
    Text(L"10:08", g_fBadge, D2D1::RectF(ring >= 0 ? rc.x + 6 : p.left, p.top, p.right - (ring >= 0 ? 6 : 0), p.bottom), Hex(0xFFFFFF));
}

// A choice card (Display ring, Appearance style): preview, title, subtitle, selected outline.
void ChoiceCard(D2D1_RECT_F r, int id, int idx, bool sel, bool soon, const wchar_t* title, const wchar_t* sub, int ring, int style) {
    Fill(r, 9, Dim(Hex(Hot(id, idx) ? 0x38383A : 0x323234), soon));
    D2D1_RECT_F pv = D2D1::RectF(r.left + 9.6f, r.top + 8, r.right - 9.6f, r.top + 8 + (style >= 0 ? 52 : 64));
    Fill(pv, 6, Dim(Hex(0x2A2D3E), soon));
    if (g_paint) {
        g_rt->PushAxisAlignedClip(pv, D2D1_ANTIALIAS_MODE_ALIASED);
        MiniPill((pv.left + pv.right) / 2, pv.top, ring, style == 1);
        g_rt->PopAxisAlignedClip();
    }
    float ty = pv.bottom + 14;
    Text(title, g_fCardT, D2D1::RectF(r.left + 9.6f, ty - 10, r.right - 24, ty + 10), Dim(Hex(0xFFFFFF), soon));
    if (soon) SoonBadge(r.right - 8, r.bottom - 16);
    Text(sub, g_fSub, D2D1::RectF(r.left + 9.6f, ty + 12, r.right - 4, ty + 30), Dim(Hex(cSub), soon));
    if (sel && g_paint) {
        g_br->SetColor(Hex(cBlue));
        g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1), 8.5f, 8.5f), g_br, 2);
        Circle(D2D1::Point2F(r.right - 15, ty), 7.2f, Hex(cBlue));
        Text(L"\xE73E", g_fIcon, D2D1::RectF(r.right - 23, ty - 8, r.right - 7, ty + 8), Hex(0xFFFFFF));
    }
    AddHit(r, soon ? I_SOON : id, idx);
}

// A mascot style card: a small closed notch with the style's glow and face, its name under it.
void MascotCard(D2D1_RECT_F r, int style, bool sel) {
    Fill(r, 9, Hex(Hot(G_PETSTYLE, style) ? 0x38383A : 0x323234));
    D2D1_RECT_F pv = D2D1::RectF(r.left + 8, r.top + 8, r.right - 8, r.top + 52);
    Fill(pv, 6, Hex(0x2A2D3E));
    if (g_paint) {
        float cx = (pv.left + pv.right) / 2, w = std::min(pv.right - pv.left - 10, 96.0f), h = 24;
        D2D1_RECT_F n = D2D1::RectF(cx - w / 2, pv.top, cx + w / 2, pv.top + h);
        ID2D1RoundedRectangleGeometry* clip = nullptr;
        g_d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(n.left, n.top - 10, n.right, n.bottom), 8, 8), &clip);
        g_rt->PushAxisAlignedClip(pv, D2D1_ANTIALIAS_MODE_ALIASED);
        if (clip) {
            g_br->SetColor(Hex(0x000000));
            g_rt->FillGeometry(clip, g_br);
            D2D1_COLOR_F col[3];
            float strength = 1;
            Mascot_Glow(style, col, &strength);
            ID2D1GradientStopCollection* stops = nullptr;
            ID2D1LinearGradientBrush* lg = nullptr;
            D2D1_GRADIENT_STOP hs[3] = {{0, col[0]}, {0.5f, col[1]}, {1, col[2]}};
            if (SUCCEEDED(g_rt->CreateGradientStopCollection(hs, 3, &stops)) &&
                SUCCEEDED(g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(n.left, 0), D2D1::Point2F(n.right, 0)), stops, &lg))) {
                lg->SetOpacity(0.87f * strength);
                g_rt->FillGeometry(clip, lg);
            }
            SafeRelease(lg);
            SafeRelease(stops);
            D2D1_GRADIENT_STOP vs[4] = {{0, D2D1::ColorF(0, 0, 0, 1)}, {0.4f, D2D1::ColorF(0, 0, 0, 0.9f)}, {0.75f, D2D1::ColorF(0, 0, 0, 0.45f)}, {1, D2D1::ColorF(0, 0, 0, 0)}};
            if (SUCCEEDED(g_rt->CreateGradientStopCollection(vs, 4, &stops)) &&
                SUCCEEDED(g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, n.top), D2D1::Point2F(0, n.bottom)), stops, &lg)))
                g_rt->FillGeometry(clip, lg);
            SafeRelease(lg);
            SafeRelease(stops);
            D2D1_MATRIX_3X2_F old;
            g_rt->GetTransform(&old);
            g_rt->SetTransform(D2D1::Matrix3x2F::Translation(0, n.top) * old); // the face is drawn from the notch's top edge
            ID2D1TransformedGeometry* moved = nullptr;
            g_d2d->CreateTransformedGeometry(clip, D2D1::Matrix3x2F::Translation(0, -n.top), &moved);
            Mascot_DrawFace(g_rt, g_d2d, g_br, style, moved, cx, h, 1, 1, 0, 0.6f);
            g_rt->SetTransform(old);
            SafeRelease(moved);
            clip->Release();
        }
        g_rt->PopAxisAlignedClip();
    }
    Text(kMascotNames[style], g_fCardT, D2D1::RectF(r.left + 9.6f, pv.bottom + 4, r.right - 24, pv.bottom + 24), Hex(0xFFFFFF));
    if (sel && g_paint) {
        g_br->SetColor(Hex(cBlue));
        g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1), 8.5f, 8.5f), g_br, 2);
        float ty = pv.bottom + 14;
        Circle(D2D1::Point2F(r.right - 15, ty), 7.2f, Hex(cBlue));
        Text(L"\xE73E", g_fIcon, D2D1::RectF(r.right - 23, ty - 8, r.right - 7, ty + 8), Hex(0xFFFFFF));
    }
    AddHit(r, G_PETSTYLE, style);
}

// Page thumbnails for the Pages list: a tiny open notch in a lilac frame with the page's colour.
void PageThumb(D2D1_RECT_F r, int kind, bool dim) {
    static const int col[PK_COUNT] = {0xFF546E, 0x8C6BFF, 0xFFC247, 0x30C759, 0x4CADFF, 0x0A85FF};
    Fill(r, 5, Dim(Hex(0x7D6FD6), dim));
    D2D1_RECT_F in = D2D1::RectF(r.left + 9, r.top, r.right - 9, r.bottom - 3);
    Fill(in, 3, Dim(Hex(0x000000), dim));
    Fill(D2D1::RectF(in.left + 4, in.top + 3, in.left + 13, in.top + 4.4f), 0.7f, Dim(Hex(0xFFFFFF), dim));
    Fill(D2D1::RectF(in.left + 4, in.top + 7, in.left + 11, in.top + 14), 2, Dim(Hex(col[kind]), dim));
    Fill(D2D1::RectF(in.left + 15, in.top + 8, in.right - 6, in.top + 9.4f), 0.7f, Dim(Hex(0xFFFFFF, 0.8f), dim));
    Fill(D2D1::RectF(in.left + 15, in.top + 11.5f, in.right - 14, in.top + 12.9f), 0.7f, Dim(Hex(0xFFFFFF, 0.5f), dim));
}

// ---- sections ----
void BuildGeneral() {
    Header(L"General", false);
    Label(L"Startup");
    Card({{RK_TOGGLE, L"Start with Windows", nullptr, G_AUTOSTART, false, {}, {}}});
    Label(L"Notch");
    Card({{RK_SEGWIDE, L"Size", L"Scales the notch, open or closed", G_SIZE, false, {L"Auto", L"100%", L"115%", L"130%", L"150%"}, {}},
          {RK_TOGGLE, L"Minimal notch", L"Shrinks to a slim tab while no music plays", G_MINIMAL, false, {}, {}},
          {RK_TOGGLE, L"Perch mascot", L"Appears in the notch when you step away", G_MASCOT, false, {}, {}},
          {RK_TOGGLE, L"Date & time", L"Shows the time on the closed notch", G_CLOCK, false, {}, {}}});
    Label(L"Mascot");
    { // seven styles, four to a row
        float cw = (kCardR - kCardL - 3 * 8) / 4, ch = 84, top = g_y;
        for (int i = 0; i < MS_COUNT; i++) {
            float x = kCardL + (i % 4) * (cw + 8), y = top + (i / 4) * (ch + 8);
            MascotCard(D2D1::RectF(x, y, x + cw, y + ch), i, GetChoice(G_PETSTYLE) == i);
        }
        g_y = top + 2 * ch + 8;
    }
    Label(L"Sounds");
    Card({{RK_TOGGLE, L"Reminder sounds", L"A chime for timers and reminders", G_SOUNDS, false, {}, {}}});
    Label(L"Full screen");
    Card({{RK_TOGGLE, L"Step aside for full screen", L"Out of the way of games, videos and presentations", G_HIDEFS, false, {}, {}}});
}
void BuildDisplay() {
    Header(L"Display", false);
    Label(L"Ring next to the clock");
    float top = g_y, h = 168;
    Fill(D2D1::RectF(kCardL, top, kCardR, top + h), 8, Hex(cCard));
    const wchar_t* t[4] = {L"Battery", L"Water", L"To-do", L"Off"};
    const wchar_t* sub[4] = {L"Charge left", L"Water today", L"Tasks finished", L"No ring"};
    float cw = (kCardR - kCardL - 16 - 3 * 8) / 4;
    for (int i = 0; i < 4; i++) {
        D2D1_RECT_F r = D2D1::RectF(kCardL + 8 + i * (cw + 8), top + 8, kCardL + 8 + i * (cw + 8) + cw, top + h - 8);
        ChoiceCard(r, D_RING, i, GetChoice(D_RING) == i, false, t[i], sub[i], i == 3 ? -1 : i == 1 ? 1 : 0, -1);
    }
    g_y = top + h;
    Label(L"Notch screen");
    Card({{RK_TEXT, L"Display", L"Pick a screen once a second display is connected", I_SOON, true, {}, {}}});
}
void BuildPages() {
    Header(L"Pages", false);
    g_y += 19.2f;
    Text(L"Pages appear in this order. Move them with the arrows.", g_fSub, D2D1::RectF(kCardL + 6.4f, g_y - 9, kCardR, g_y + 9), Hex(cSub));
    g_y += 10;
    int order[PK_COUNT], n = 0;
    { // every page, shown ones first in their order, then the hidden ones
        int shown[PK_COUNT], ns = Settings_PageOrder(g_s, shown);
        for (int i = 0; i < ns; i++) order[n++] = shown[i];
        static const int deflt[] = {PK_MUSIC, PK_POMODORO, PK_WATER, PK_SHELF, PK_TODO, PK_WIDGETS};
        for (int k : deflt) {
            bool in = false;
            for (int i = 0; i < n; i++) in |= order[i] == k;
            if (!in) order[n++] = k;
        }
    }
    for (int part = 0; part < 2; part++) {
        Text(part == 0 ? L"IN YOUR NOTCH" : L"HIDDEN", g_fCaps, D2D1::RectF(kCardL + 12, g_y, kCardR, g_y + 16), Hex(0x9A9AA0));
        g_y += 18;
        std::vector<int> rows;
        for (int i = 0; i < n; i++) if (Settings_PageHidden(g_s, order[i]) == (part == 1)) rows.push_back(order[i]);
        if (rows.empty()) { g_y += 6; continue; }
        float top = g_y, rh = 32;
        Fill(D2D1::RectF(kCardL, top, kCardR, top + rh * rows.size()), 8, Hex(cCard));
        for (size_t i = 0; i < rows.size(); i++) {
            int k = rows[i];
            float y = top + i * rh, cy = y + rh / 2;
            bool hidden = part == 1;
            if (i > 0) Fill(D2D1::RectF(kCardL + 32, y, kCardR, y + 1), 0, Hex(cDivider));
            if (!hidden) {
                wchar_t num[8];
                swprintf(num, 8, L"%d", (int)i + 1);
                Text(num, g_fCenter, D2D1::RectF(kCardL + 4, cy - 10, kCardL + 28, cy + 10), Hex(cSub));
            }
            PageThumb(D2D1::RectF(kCardL + 32, y + 4, kCardL + 104, y + rh - 4), k, hidden);
            Text(kPageTitles[k], g_fRow, D2D1::RectF(kCardL + 116, cy - 10, kCardR - 120, cy + 10), Dim(Hex(0xFFFFFF), hidden));
            if (k == PK_WATER || k == PK_WIDGETS) SoonBadge(kCardR - 120, cy);
            if (!hidden) { // move up / down
                for (int dir : {-1, 1}) {
                    D2D1_RECT_F a = D2D1::RectF(kCardR - 92, dir < 0 ? cy - 11 : cy, kCardR - 72, dir < 0 ? cy : cy + 11);
                    bool can = dir < 0 ? i > 0 : i + 1 < rows.size();
                    Text(dir < 0 ? L"\xE70E" : L"\xE70D", g_fIcon, a, Hex(Hot(dir < 0 ? P_UP : P_DOWN, k) ? 0xFFFFFF : 0x8E8E93, can ? 1 : 0.3f));
                    if (can) AddHit(a, dir < 0 ? P_UP : P_DOWN, k);
                }
            }
            Toggle(kCardR - kPadIn, cy, P_TOGGLE, false, !hidden);
            g_hits.back().arg = k;
        }
        g_y = top + rh * rows.size() + 10;
    }
    Text(L"Water and Widgets are coming soon; they show a placeholder for now.", g_fSub, D2D1::RectF(kCardL + 6.4f, g_y, kCardR, g_y + 16), Hex(cSub));
    g_y += 20;
}
void BuildAppearance() {
    Header(L"Appearance", false);
    Label(L"Notch style");
    float top = g_y, h = 128;
    Fill(D2D1::RectF(kCardL, top, kCardR, top + h), 8, Hex(cCard));
    float cw = (kCardR - kCardL - 16 - 8) / 2;
    const wchar_t* t[2] = {L"Classic", L"Floating"};
    const wchar_t* sub[2] = {L"Attached to the top of the screen", L"Hovers a little below the top"};
    for (int i = 0; i < 2; i++)
        ChoiceCard(D2D1::RectF(kCardL + 8 + i * (cw + 8), top + 8, kCardL + 8 + i * (cw + 8) + cw, top + h - 8), A_STYLE, i, GetChoice(A_STYLE) == i, false, t[i], sub[i], 0, i);
    g_y = top + h + 8;
    Card({{RK_TOGGLE, L"Glass", L"Blurs what's behind the open notch", A_GLASS, false, {}, {}},
          {RK_STEP, L"Glass tint", nullptr, A_TINT, false, {}, {}}});
    Label(L"Notch");
    Card({{RK_SEG, L"Open the notch", nullptr, A_OPEN, false, {L"Hover", L"Click"}, {}},
          {RK_TOGGLE, L"Page icons", L"Icons appear when you point at the page dots", A_PAGEICONS, false, {}, {}}});
    Label(L"Motion");
    Card({{RK_SEG, L"Animation speed", nullptr, A_SPEED, false, {L"Normal", L"Fast"}, {}},
          {RK_TOGGLE, L"Motion blur", L"A soft blur while the notch moves", I_SOON, true, {}, {}}});
}
void BuildMusic() {
    Header(L"Music and lyrics", false);
    Label(L"Lyrics");
    Card({{RK_TOGGLE, L"Show lyrics", L"From LRCLIB: sends each song's title and artist", M_LYRICS, false, {}, {}},
          {RK_SEGWIDE, L"Show lyrics earlier", L"Shift lyrics if they run behind the song", M_EARLIER, false, {L"Off", L"0.3 s", L"0.6 s", L"1 s"}, {}}});
}
void BuildWidgets() {
    Header(L"Widgets", true);
    Label(L"Widgets page");
    Card({{RK_TOGGLE, L"Show Widgets page", L"A page for the widgets you pick", I_SOON, true, {}, {}}});
    Label(L"Choose widgets");
    Card({{RK_SEG, L"Side", nullptr, I_SOON, true, {L"Left", L"Right"}, {}},
          {RK_TEXT, L"This week", nullptr, I_SOON, true, {}, {}},
          {RK_TEXT, L"Water", nullptr, I_SOON, true, {}, {}},
          {RK_TEXT, L"Focus today", nullptr, I_SOON, true, {}, {}},
          {RK_TEXT, L"World clock", nullptr, I_SOON, true, {}, {}},
          {RK_TEXT, L"Battery", nullptr, I_SOON, true, {}, {}},
          {RK_TEXT, L"Moon", nullptr, I_SOON, true, {}, {}},
          {RK_TEXT, L"Countdown", nullptr, I_SOON, true, {}, {}}});
}
void BuildAsk() {
    Header(L"Ask Perch", false);
    Label(L"Gemini");
    static wchar_t status[64];
    wcscpy(status, L"Not set");
    if (g_host.askStatus) g_host.askStatus(status, 64);
    bool hasKey = wcscmp(status, L"Not set") != 0;
    std::vector<std::wstring> models;
    if (hasKey && g_host.askModels) g_host.askModels(models);
    Card({{RK_BUTTONS, L"Gemini API key", status, K_PASTE, false, hasKey ? std::vector<std::wstring>{L"Paste new", L"Remove"} : std::vector<std::wstring>{L"Paste key"},
           hasKey ? std::vector<int>{K_PASTE, K_REMOVE} : std::vector<int>{K_PASTE}},
          {RK_LABEL, L"Free key: aistudio.google.com > Get API key, copy it, then Paste key", nullptr, I_NONE, false, {}, {}}});
    Label(L"Model");
    Card({{RK_STEP, L"Model", models.empty() ? (hasKey ? L"From your key" : L"Add a key first") : L"Pick with the arrows", K_MODEL, false, {}, {}}});
    Label(L"Ask");
    bool off = !_wcsicmp(g_s.askHotkey.c_str(), L"Off"), taken = !off && g_host.askHotkeyOk && !g_host.askHotkeyOk();
    std::wstring shown = g_recordKey ? L"Press keys\x2026" : off ? L"Set shortcut" : g_s.askHotkey;
    for (size_t p; !g_recordKey && (p = shown.find(L'+')) != std::wstring::npos && p + 1 < shown.size();) shown.replace(p, 1, L" \x00B7 "); // Ctrl · Alt · P
    const wchar_t* sub = g_recordKey ? L"Press keys with Ctrl, Alt or Win"
                       : taken ? L"Taken by another app. Click to change"
                       : off ? L"Off (right-click > Ask Perch works)"
                             : L"Opens Ask Perch. Click to change";
    Card({{RK_BUTTONS, L"Shortcut", sub, K_HOTKEY, false, off ? std::vector<std::wstring>{shown} : std::vector<std::wstring>{shown, L"Turn off"},
           off ? std::vector<int>{K_HOTKEY} : std::vector<int>{K_HOTKEY, K_HOTKEY_OFF}},
          {RK_TEXT, L"Privacy", L"Questions go only to Google, using your key", I_NONE, false, {}, {}}});
}
void BuildTodo() {
    Header(L"To-do", false);
    Label(L"Reminders");
    Card({{RK_TOGGLE, L"When a task starts", nullptr, T_START, false, {}, {}},
          {RK_TOGGLE, L"When time is up", nullptr, T_END, false, {}, {}},
          {RK_SEG, L"Snooze for", nullptr, T_SNOOZE, false, {L"5 min", L"10 min", L"15 min"}, {}}});
    Label(L"Calendar options");
    Card({{RK_SEG, L"Week starts on", nullptr, T_WEEK, false, {L"Sunday", L"Monday"}, {}},
          {RK_SEG, L"Default view", nullptr, T_VIEW, false, {L"Month", L"Week"}, {}},
          {RK_SEGWIDE, L"Event reminders", L"A pop-up before each timed event today", T_REMIND, false, {L"Off", L"At start", L"5 min", L"10 min", L"15 min", L"30 min"}, {}}});
    Label(L"Google Calendar");
    wchar_t status[128] = L"Not connected";
    if (g_host.calStatus) g_host.calStatus(status, 128);
    static std::wstring st;
    st = status;
    Row link = {RK_BUTTONS, L"Calendar link", st.c_str(), T_PASTE, false, {L"Paste link"}, {T_PASTE}};
    if (!g_s.calLink.empty()) { link.opts = {L"Paste link", L"Refresh", L"Remove"}; link.ids = {T_PASTE, T_REFRESH, T_REMOVE}; }
    Card({link, {RK_LABEL, L"Google Calendar > Settings > your calendar > Secret address in iCal format", nullptr, I_NONE, false, {}, {}}});
}
void BuildPomodoro() {
    Header(L"Pomodoro", false);
    Label(L"Durations");
    Card({{RK_STEP, L"Focus", nullptr, O_FOCUS, false, {}, {}},
          {RK_STEP, L"Short break", nullptr, O_SHORT, false, {}, {}},
          {RK_STEP, L"Long break", nullptr, O_LONG, false, {}, {}}});
}
void BuildWater() {
    Header(L"Water", false);
    Label(L"Reminders");
    Card({{RK_TOGGLE, L"Water reminders", L"A nudge when you're behind, until you reach your goal", W_REMIND, false, {}, {}}});
    Label(L"Plan");
    Card({{RK_SEG, L"Daily goal", nullptr, W_GOAL, false, {L"1.5 L", L"2 L", L"2.5 L", L"3 L"}, {}},
          {RK_STEP, L"Cup size", L"What one tap of + adds", W_CUP, false, {}, {}},
          {RK_STEP, L"Active from", nullptr, W_FROM, false, {}, {}},
          {RK_STEP, L"Active until", nullptr, W_UNTIL, false, {}, {}}});
    Label(L"Today");
    wchar_t today[64] = L"";
    if (g_host.waterStatus) g_host.waterStatus(today, 64);
    Card({{RK_TEXT, L"Today's progress", nullptr, I_NONE, false, {today}, {}}});
}
void BuildShelf() {
    Header(L"Shelf", false);
    Label(L"Shelf");
    Card({{RK_TOGGLE, L"Empty shelf at start", L"Clears the shelf each time Perch starts", S_EMPTY, false, {}, {}}});
    Label(L"Clipboard history");
    Card({{RK_TOGGLE, L"Clipboard history", L"Keeps copied text in the Shelf to paste later", C_HISTORY, false, {}, {}},
          {RK_STEP, L"Keep up to", L"Up to 50 items", C_MAX, false, {}, {}},
          {RK_TOGGLE, L"Keep after restart", L"Encrypted, only on this PC", C_REMEMBER, false, {}, {}}});
    Label(L"Smart OCR");
    Card({{RK_TEXT, L"Copy text from screen", L"Drag over the screen to copy the text in it", I_SOON, true, {L"Ctrl + Alt + T"}, {}}});
}
void BuildHud() {
    Header(L"HUD & popups", false);
    Label(L"Indicators");
    Card({{RK_TOGGLE, L"Volume", nullptr, H_VOLUME, false, {}, {}},
          {RK_TOGGLE, L"Brightness", nullptr, H_BRIGHT, false, {}, {}},
          {RK_TOGGLE, L"Caps Lock", nullptr, H_CAPS, false, {}, {}},
          {RK_TOGGLE, L"Input language", nullptr, H_LANG, false, {}, {}}});
    Label(L"Popups");
    Card({{RK_TOGGLE, L"Battery", nullptr, H_BATTERY, false, {}, {}},
          {RK_TOGGLE, L"Wi-Fi", nullptr, H_WIFI, false, {}, {}},
          {RK_TOGGLE, L"Bluetooth headset", nullptr, H_BT, false, {}, {}}});
}
void BuildAbout() {
    float cx = (kCardL + kCardR) / 2;
    D2D1_RECT_F logo = D2D1::RectF(cx - 32, 38, cx + 32, 102);
    if (g_paint && g_logo) g_rt->DrawBitmap(g_logo, logo);
    Text(L"Perch", g_fBig, D2D1::RectF(kCardL, 106, kCardR, 132), Hex(0xFFFFFF));
    wchar_t v[64];
    swprintf(v, 64, L"Version %ls", kVersion);
    Text(v, g_fCenter, D2D1::RectF(kCardL, 132, kCardR, 150), Hex(cSub));
    Text(L"A notch for Windows", g_fCenter, D2D1::RectF(kCardL, 148, kCardR, 166), Hex(cSub));
    g_y = 180;
    Label(L"Info");
    Card({{RK_TEXT, L"Calendar data", nullptr, I_NONE, false, {L"Only from your own calendar link"}, {}}});
    Label(L"Settings");
    Card({{RK_BUTTONS, L"settings.ini", L"All options, with comments, in a text file", B_FILE, false, {L"Edit file"}, {B_FILE}},
          {RK_BUTTONS, L"Reset to defaults", L"Back to how Perch started", B_RESET, false, {L"Reset"}, {B_RESET}}});
}

// ---- the whole window ----
void Build() {
    g_hits.clear();
    if (g_paint) g_rt->Clear(Hex(cContent));
    // sidebar
    Fill(D2D1::RectF(0, 0, kSideW, kH), 0, Hex(cSide));
    Circle(D2D1::Point2F(20, 20), 5.6f, Hex(0xFF5E57)); // close, like macOS: an "x" shows while the pointer is on it
    if (Hot(I_CLOSE) && g_paint) {
        g_br->SetColor(Hex(0x4D0000, 0.85f));
        g_rt->DrawLine(D2D1::Point2F(17.6f, 17.6f), D2D1::Point2F(22.4f, 22.4f), g_br, 1.2f);
        g_rt->DrawLine(D2D1::Point2F(22.4f, 17.6f), D2D1::Point2F(17.6f, 22.4f), g_br, 1.2f);
    }
    AddHit(D2D1::RectF(10, 10, 30, 30), I_CLOSE);
    for (int s = 0; s < SEC_COUNT; s++) {
        float cy = s == SEC_ABOUT ? 527.2f : 59.2f + 32 * s;
        D2D1_RECT_F row = D2D1::RectF(10.4f, cy - 14.4f, 181.6f, cy + 14.4f);
        if (s == g_sec) Fill(row, 7, Hex(cSel));
        else if (Hot(I_SECTION, s)) Fill(row, 7, Hex(0x202022));
        D2D1_RECT_F ic = D2D1::RectF(17.6f, cy - 11.2f, 40, cy + 11.2f);
        Fill(ic, 5.6f, Hex(kSections[s].color));
        Text(kSections[s].glyph, g_fSideIcon, ic, Hex(0xFFFFFF));
        Text(kSections[s].name, g_fItem, D2D1::RectF(49.6f, cy - 10, 176, cy + 10), Hex(0xFFFFFF));
        AddHit(row, I_SECTION, s);
    }
    Fill(D2D1::RectF(10.4f, 504, 181.6f, 505), 0, Hex(0x1F1F21));
    // content, scrolled
    if (g_paint) {
        g_rt->PushAxisAlignedClip(D2D1::RectF(kSideW, 0, kW, kH), D2D1_ANTIALIAS_MODE_ALIASED);
        g_rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -g_scroll));
    }
    size_t firstContentHit = g_hits.size();
    g_y = 62;
    switch (g_sec) {
    case SEC_GENERAL: BuildGeneral(); break;
    case SEC_DISPLAY: BuildDisplay(); break;
    case SEC_PAGES: BuildPages(); break;
    case SEC_APPEARANCE: BuildAppearance(); break;
    case SEC_MUSIC: BuildMusic(); break;
    case SEC_WIDGETS: BuildWidgets(); break;
    case SEC_ASK: BuildAsk(); break;
    case SEC_TODO: BuildTodo(); break;
    case SEC_POMODORO: BuildPomodoro(); break;
    case SEC_WATER: BuildWater(); break;
    case SEC_SHELF: BuildShelf(); break;
    case SEC_HUD: BuildHud(); break;
    case SEC_ABOUT: BuildAbout(); break;
    }
    g_contentH = g_y + 20;
    for (size_t i = firstContentHit; i < g_hits.size(); i++) { g_hits[i].r.top -= g_scroll; g_hits[i].r.bottom -= g_scroll; }
    if (g_paint) {
        g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
        g_rt->PopAxisAlignedClip();
    }
}

void LoadLogo(HINSTANCE inst) {
    SafeRelease(g_logo);
    HICON icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, (int)(64 * g_scale), (int)(64 * g_scale), 0);
    IWICImagingFactory* wic = nullptr;
    IWICBitmap* bmp = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (icon && SUCCEEDED(CoCreateInstance(kCLSID_WIC, nullptr, CLSCTX_INPROC_SERVER, kIID_IWICImagingFactory, (void**)&wic)) &&
        SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bmp)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(bmp, kWICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        g_rt->CreateBitmapFromWicBitmap(conv, nullptr, &g_logo);
    SafeRelease(conv);
    SafeRelease(bmp);
    SafeRelease(wic);
    if (icon) DestroyIcon(icon);
}

HINSTANCE g_inst;
bool CreateTarget() {
    if (g_rt) return true;
    RECT rc;
    GetClientRect(g_wnd, &rc);
    float dpi = 96.0f * g_scale;
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), dpi, dpi);
    if (FAILED(g_d2d->CreateHwndRenderTarget(p, D2D1::HwndRenderTargetProperties(g_wnd, D2D1::SizeU(rc.right, rc.bottom)), &g_rt))) return false;
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_rt->CreateSolidColorBrush(Hex(0xFFFFFF), &g_br);
    LoadLogo(g_inst);
    return true;
}
void Paint() {
    if (!g_rt) return;
    g_rt->BeginDraw();
    g_paint = true;
    Build();
    g_paint = false;
    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) { SafeRelease(g_logo); SafeRelease(g_br); SafeRelease(g_rt); }
}
void Redraw() { if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE); }
void Relayout() { g_paint = false; if (g_rt) Build(); }

void ReleaseAll() {
    SafeRelease(g_logo);
    SafeRelease(g_br);
    SafeRelease(g_rt);
    for (IDWriteTextFormat** f : {&g_fStep, &g_fCaps, &g_fSideIcon, &g_fTitle, &g_fItem, &g_fRow, &g_fSub, &g_fLabel, &g_fSeg, &g_fIcon, &g_fIconBig, &g_fBadge, &g_fCardT, &g_fCenter, &g_fBig, &g_fRight})
        SafeRelease(*f);
    SafeRelease(g_dw);
    SafeRelease(g_d2d);
}

const Hit* HitAt(float x, float y) {
    Relayout();
    for (const Hit& h : g_hits)
        if (x >= h.r.left && x < h.r.right && y >= h.r.top && y < h.r.bottom && (h.id == I_SECTION || h.id == I_CLOSE || x >= kSideW)) return &h;
    return nullptr;
}

void SetSection(int s) {
    g_sec = s < 0 || s >= SEC_COUNT ? SEC_GENERAL : s;
    g_scroll = 0;
    g_recordKey = false;
    Redraw();
}

void MovePage(int kind, int dir) {
    int order[PK_COUNT], n = 0;
    int shown[PK_COUNT], ns = Settings_PageOrder(g_s, shown);
    for (int i = 0; i < ns; i++) order[n++] = shown[i];
    bool hidden[PK_COUNT] = {};
    for (int k = 0; k < PK_COUNT; k++) {
        hidden[k] = Settings_PageHidden(g_s, k);
        if (hidden[k]) order[n++] = k;
    }
    for (int i = 0; i < ns; i++)
        if (order[i] == kind) {
            int j = i + dir;
            if (dir != 0 && j >= 0 && j < ns) { order[i] = order[j]; order[j] = kind; }
            break;
        }
    if (dir == 0) {
        hidden[kind] = !hidden[kind];
        int visible = 0;
        for (int k = 0; k < PK_COUNT; k++) visible += !hidden[k];
        if (visible == 0) hidden[kind] = false; // keep at least one page
    }
    Settings_SetPages(g_s, order, n, hidden);
    g_host.commit(g_s);
}

void Activate(const Hit& h) {
    switch (h.id) {
    case I_CLOSE: DestroyWindow(g_wnd); return;
    case I_SECTION: SetSection(h.arg); return;
    case P_UP: MovePage(h.arg, -1); break;
    case P_DOWN: MovePage(h.arg, 1); break;
    case P_TOGGLE: MovePage(h.arg, 0); break;
    case G_SIZE: case A_OPEN: case A_SPEED: case T_SNOOZE: case T_WEEK: case T_VIEW: case D_RING: case A_STYLE: case G_PETSTYLE: case W_GOAL: case T_REMIND: case M_EARLIER: SetChoice(h.id, h.arg); break;
    case A_TINT: case O_FOCUS: case O_SHORT: case O_LONG: case W_CUP: case W_FROM: case W_UNTIL: case C_MAX: case K_MODEL: Step(h.id, h.arg); break;
    case T_PASTE: if (g_host.calPaste) g_host.calPaste(); break;
    case T_REFRESH: if (g_host.calRefresh) g_host.calRefresh(); break;
    case T_REMOVE: if (g_host.calRemove) g_host.calRemove(); break;
    case B_FILE: if (g_host.openFile) g_host.openFile(); break;
    case K_PASTE: if (g_host.askPaste) g_host.askPaste(); break;
    case K_REMOVE: if (g_host.askRemove) g_host.askRemove(); break;
    case K_HOTKEY: g_recordKey = !g_recordKey; break; // the next keys pressed become the shortcut
    case K_HOTKEY_OFF: g_recordKey = false; g_s.askHotkey = L"Off"; g_host.commit(g_s); break;
    case B_RESET: {
        Settings d;
        d.calLink = g_s.calLink; // the calendar link isn't a preference: keep it
        g_s = d;
        g_host.commit(g_s);
        break;
    }
    default: FlipBool(h.id);
    }
    Redraw();
}

int g_pressId = -1, g_pressArg = 0;

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (CreateTarget()) Paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCHITTEST: { // drag the window by the top of the sidebar and the title area
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        if (r != HTCLIENT) return r;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        float x = pt.x / g_scale, y = pt.y / g_scale;
        if (y < 44 && !(x < 32 && y < 32) && !(x > kCardR - 140 && x > kSideW)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE:
        if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;
    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.0f;
        SafeRelease(g_logo);
        SafeRelease(g_br);
        SafeRelease(g_rt);
        const RECT* r = (const RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const Hit* ht = HitAt(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale);
        int id = ht ? ht->id : -1, arg = ht ? ht->arg : 0;
        if (id != g_hotId || arg != g_hotArg) { g_hotId = id; g_hotArg = arg; Redraw(); }
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hotId != -1) { g_hotId = -1; Redraw(); }
        return 0;
    case WM_LBUTTONDOWN: {
        const Hit* ht = HitAt(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale);
        g_pressId = ht ? ht->id : -1;
        g_pressArg = ht ? ht->arg : 0;
        if (ht) SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        const Hit* ht = HitAt(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale);
        if (ht && ht->id == g_pressId && ht->arg == g_pressArg) { Hit copy = *ht; Activate(copy); }
        g_pressId = -1;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        Relayout();
        float maxScroll = std::fmax(0.0f, g_contentH - kH);
        g_scroll = std::fmin(maxScroll, std::fmax(0.0f, g_scroll - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 40));
        Redraw();
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_recordKey) { // recording the Ask Perch shortcut
            if (wp == VK_ESCAPE) { g_recordKey = false; Redraw(); return 0; }
            if (wp == VK_SHIFT || wp == VK_CONTROL || wp == VK_MENU || wp == VK_LWIN || wp == VK_RWIN) return 0;
            auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }; // the keys held right now
            UINT mods = (down(VK_CONTROL) ? MOD_CONTROL : 0) | (down(VK_MENU) ? MOD_ALT : 0) | (down(VK_SHIFT) ? MOD_SHIFT : 0) |
                        (down(VK_LWIN) || down(VK_RWIN) ? MOD_WIN : 0);
            std::wstring t = Settings_HotkeyText(mods, (UINT)wp);
            if (t.empty() || !(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN))) return 0; // needs Ctrl, Alt or Win, and a key with a name
            g_recordKey = false;
            g_s.askHotkey = t;
            g_host.commit(g_s);
            Redraw();
            return 0;
        }
        if (msg == WM_SYSKEYDOWN) break;
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == VK_DOWN || wp == VK_UP) { SetSection((g_sec + (wp == VK_DOWN ? 1 : SEC_COUNT - 1)) % SEC_COUNT); return 0; }
        return 0;
    case WM_APP + 1: // tests: wp = section to show (or -1); returns the section shown
        if ((int)wp >= 0) SetSection((int)wp);
        return g_sec;
    case WM_APP + 2: { // tests: wp = control id, lp = arg; returns MAKELONG(x, y) client px of its centre, or -1
        Relayout();
        for (const Hit& ht : g_hits)
            if (ht.id == (int)wp && ht.arg == (int)lp)
                return MAKELONG((int)((ht.r.left + ht.r.right) / 2 * g_scale), (int)((ht.r.top + ht.r.bottom) / 2 * g_scale));
        return -1;
    }
    case WM_DESTROY:
        ReleaseAll();
        g_wnd = nullptr;
        g_recordKey = false;
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); // hand the window's pages back
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

bool SettingsUI_IsOpen() { return g_wnd != nullptr; }

void SettingsUI_Refresh(const Settings& s) {
    if (!g_wnd) return;
    g_s = s;
    Redraw();
}

void SettingsUI_Show(HINSTANCE inst, const Settings& current, const SettingsUIHost& host, int section) {
    g_host = host;
    if (g_wnd) { // already open: bring it forward (on the section asked for)
        if (section >= 0) SetSection(section);
        ShowWindow(g_wnd, SW_RESTORE);
        SetForegroundWindow(g_wnd);
        return;
    }
    g_inst = inst;
    g_s = current;
    g_sec = section >= 0 ? section : SEC_GENERAL;
    g_scroll = 0;
    g_hotId = -1;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, kIID_ID2D1Factory, nullptr, (void**)&g_d2d))) return;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, kIID_IDWriteFactory, (IUnknown**)&g_dw))) { ReleaseAll(); return; }
    const wchar_t* ui = L"Segoe UI";
    const wchar_t* icons = L"Segoe Fluent Icons";
    {
        IDWriteFontCollection* fc = nullptr;
        UINT32 idx;
        BOOL exists = FALSE;
        if (SUCCEEDED(g_dw->GetSystemFontCollection(&fc))) { fc->FindFamilyName(icons, &idx, &exists); fc->Release(); }
        if (!exists) icons = L"Segoe MDL2 Assets";
    }
    g_fTitle = Format(ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 17, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fItem = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 12.4f, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fRow = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 12.2f, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fSub = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 10.4f, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fLabel = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 10.4f, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fSeg = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 10.4f, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fIcon = Format(icons, DWRITE_FONT_WEIGHT_NORMAL, 12, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fSideIcon = Format(icons, DWRITE_FONT_WEIGHT_BOLD, 13.6f, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fIconBig = Format(icons, DWRITE_FONT_WEIGHT_NORMAL, 20, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fBadge = Format(ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 8.8f, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fCardT = Format(ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.4f, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fCenter = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 11.2f, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fBig = Format(ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 20, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fRight = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 11.2f, DWRITE_TEXT_ALIGNMENT_TRAILING);
    g_fBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fCaps = Format(ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 9.6f, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fStep = Format(ui, DWRITE_FONT_WEIGHT_NORMAL, 15, DWRITE_TEXT_ALIGNMENT_CENTER);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof wc};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(28, 28, 30));
        wc.lpszClassName = kClass;
        wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        RegisterClassExW(&wc);
        registered = true;
    }

    // Centre on the monitor under the pointer, sized for its DPI.
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    if (shcore) {
        if (auto f = (GetDpiForMonitorFn)(void*)GetProcAddress(shcore, "GetDpiForMonitor")) f(mon, 0, &dpiX, &dpiY);
        FreeLibrary(shcore);
    }
    g_scale = dpiX / 96.0f;
    int w = (int)std::ceil(kW * g_scale), hgt = (int)std::ceil(kH * g_scale);
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - hgt) / 2;
    // A borderless window (its own red close dot), with Windows 11's rounded corners and shadow.
    g_wnd = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Perch Settings", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, w, hgt, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) { ReleaseAll(); return; }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    int round = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(g_wnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof round);
    MARGINS m = {0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(g_wnd, &m); // keeps the drop shadow on a borderless window
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);
}
