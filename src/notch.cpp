// Notch for Windows - native Win32 + Direct2D notch overlay.
//
// Perch: a notch for Windows.
// Collapsed: a small black pill at the top-centre of the primary monitor.
// Hover:     opens into pages (Music, Shelf...), flipped with the wheel, the arrows or the page icons.
// Pop-ups:   volume, brightness, battery, output device.
// Full screen apps/games: the notch steps aside (hides) and comes back after.
// Right-click: menu (Start with Windows, Settings, Quit).
//
// Idle cost: everything is event-driven. No timers run while collapsed.
//
// Test hooks: WM_APP_STATE returns a bitmask of the current state, and menu
// commands can be sent as WM_COMMAND. See tests/.

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wlanapi.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <initializer_list>
#include "media.h"
#include "settings.h"
#include "settingsui.h"
#include "calendar.h"
#include "mascot.h"
#include "ask.h"

#ifdef _MSC_VER
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#endif

// GUIDs defined locally so the code builds the same with MSVC and MinGW.
static const GUID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID kIID_IMMDeviceEnumerator  = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID kIID_IAudioEndpointVolume = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};
static const GUID kIID_ID2D1Factory         = {0x06152247, 0x6F50, 0x465A, {0x92, 0x45, 0x11, 0x8B, 0xFD, 0x3B, 0x60, 0x07}};
static const GUID kIID_IDWriteFactory       = {0xB859EE5A, 0xD838, 0x4B5B, {0xA2, 0xE8, 0x1A, 0xDC, 0x7D, 0x93, 0xDB, 0x48}};
static const GUID kIID_IAudioSessionManager2      = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
static const GUID kIID_IAudioSessionNotification  = {0x641DD20B, 0x4D41, 0x49CC, {0xAB, 0xA3, 0x17, 0x4B, 0x94, 0x77, 0xBB, 0x08}};
static const GUID kIID_IAudioSessionEvents        = {0x24918ACC, 0x64B3, 0x37C1, {0x8C, 0xA9, 0x74, 0xA6, 0x6E, 0x99, 0x57, 0xA8}};
static const GUID kIID_IUnknown                   = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIID_IAudioEndpointVolumeCallback = {0x657804FA, 0xD6AD, 0x4496, {0x8A, 0x60, 0x35, 0x27, 0x52, 0xAF, 0x4F, 0x89}};
static const GUID kIID_IMMNotificationClient      = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const PROPERTYKEY kPKEY_Device_FriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
static const GUID kCLSID_WICImagingFactory = {0xCACAF262, 0x9370, 0x4615, {0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A}};
static const GUID kIID_IWICImagingFactory  = {0xEC5EC8A9, 0xC395, 0x4314, {0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70}};
static const GUID kIID_IShellItemImageFactory = {0xBCC18B79, 0xBA16, 0x442F, {0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B}};
static const GUID kIID_IDataObject         = {0x0000010E, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIID_IDropTarget         = {0x00000122, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kBHID_DataObject         = {0xB8C0BD9F, 0xED24, 0x455C, {0x83, 0xE6, 0xD5, 0x39, 0x0C, 0x4F, 0xE8, 0xC4}};
static const GUID kGUID_ACDC_POWER_SOURCE           = {0x5D3E9A59, 0xE9D5, 0x4B00, {0xA6, 0xBD, 0xFF, 0x34, 0xFF, 0x51, 0x65, 0x48}};
static const GUID kGUID_BATTERY_PERCENTAGE_REMAINING = {0xA7AD8041, 0xB45A, 0x4CAE, {0x87, 0xA3, 0xEE, 0xCB, 0xB4, 0x68, 0xA9, 0xE1}};
static const GUID kGUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS = {0x8FFEE2C6, 0x2D01, 0x46BE, {0xAD, 0xB9, 0x39, 0x8A, 0xDD, 0xC5, 0xB4, 0xFF}};
static const PROPERTYKEY kPKEY_AudioEndpoint_FormFactor = {{0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 0};

// ---------- layout (DIPs) ----------
static const float kWinW = 440, kWinH = 250;            // fixed window; transparent pixels click through
static const float kExpW = 395.6f, kExpH = 172, kExpR = 32; // open notch (pages): 395.6 x 172, corners 32
static const float kShoulderClosed = 9, kShoulderOpen = 22;   // curved shoulders where the notch meets the top edge
static const float kToastH = 110;                        // open notch while a pop-up shows
static const float kIconBarH = 30;                       // extra height while the page icons show
static const float kArtSize = 62;                        // album art on the Music page
static const float kCtlR = 16;                           // media button hit radius
static const float kDotsY = 164, kArrowY = 94;           // page dots and the < > arrows

// ---------- pages (open notch), in the order the dots show them ----------
// Page kinds (settings.h PageKind). The pages shown and their order come from [Pages] in settings.ini
// (default: Music, Pomodoro, Water, Shelf, To-do; Widgets switched off).
enum Page { PAGE_MUSIC = PK_MUSIC, PAGE_POMODORO = PK_POMODORO, PAGE_SHELF = PK_SHELF, PAGE_TODO = PK_TODO,
            PAGE_WATER = PK_WATER, PAGE_WIDGETS = PK_WIDGETS, PAGE_COUNT = PK_COUNT };
static const int kPageColor[PAGE_COUNT] = {0xFF546E, 0x8C6BFF, 0xFFC247, 0x30C759, 0x4CADFF, 0x0A85FF}; // the current page's icon
static int g_pages[PAGE_COUNT] = {PAGE_MUSIC, PAGE_POMODORO, PAGE_WATER, PAGE_SHELF, PAGE_TODO}; // shown, in order
static int g_pageN = 5;

// ---------- messages, ids ----------
enum {
    WM_APP_STATE = WM_APP + 1,   // returns StateFlags (used by tests)
    WM_APP_APPBAR = WM_APP + 3,  // shell appbar notifications (fullscreen)
    WM_APP_MEDIA = WM_APP + 4,   // now-playing info changed
    WM_APP_VOLUME = WM_APP + 5,  // output volume changed: wp = level*1000, lp = muted
    WM_APP_OUTPUT_CHANGED = WM_APP + 6, // default output device changed
    WM_APP_TOAST_LEVEL = WM_APP + 7,    // returns toast level*1000, or -1 (used by tests)
    WM_APP_BATTERY = WM_APP + 8,        // returns (onAC << 8) | percent as last reported by Windows, or -1 (tests)
    WM_APP_SIM_POWER = WM_APP + 9,      // tests: simulate a power report, wp = onAC, lp = percent
    WM_APP_BRIGHTNESS = WM_APP + 10,    // returns the last brightness Windows reported, or -1 (tests)
    WM_APP_SHELF_COUNT = WM_APP + 11,   // returns the number of shelf items (tests)
    WM_APP_SHELF_TILE = WM_APP + 12,    // wp = slot; returns MAKELONG(x, y) client px of that tile's centre, or -1 (tests)
    WM_APP_SETTINGS_CHANGED = WM_APP + 14, // something changed in the data folder (settings watcher)
    WM_APP_SETTING = WM_APP + 15,       // tests: wp = SettingQuery; returns the value in use
    WM_APP_SHELF_CLOSE = WM_APP + 16,   // tests: wp = slot; MAKELONG(x, y) client px of its remove button, or -1
    WM_APP_PILL = WM_APP + 17,          // tests: wp 0 = MAKELONG(left, top), 1 = MAKELONG(right, bottom) of the pill, screen px
    WM_APP_PAGE = WM_APP + 19,          // tests: wp = page to show at once, or -1; returns the page shown
    WM_APP_HOT = WM_APP + 20,           // tests: wp = Hot id; returns MAKELONG(x, y) client px of its centre
    WM_APP_KEY = WM_APP + 21,           // keyboard hook: wp = key (Caps Lock pressed, or a language-switch key released)
    WM_APP_BT_DEVICE = WM_APP + 22,     // an audio output became active: lp = _wcsdup'd device id (0 from tests: "Test Headset")
    WM_APP_WIFI = WM_APP + 23,          // Wi-Fi: wp = 1 connected / 0 disconnected, lp = _wcsdup'd network name (0 from tests)
    WM_APP_SIM_LANG = WM_APP + 24,      // tests: wp = LANGID; shows the input language pop-up for it
    WM_APP_BT_COUNT = WM_APP + 25,      // tests: number of Bluetooth audio outputs Windows knows (any state)
    WM_APP_POMO = WM_APP + 26,          // tests: wp 0 = state | phase << 2 | session << 4 | seconds left << 8;
                                        //        1 = end the current phase now; 2 = chimes played; 3 = today's focus minutes
    WM_APP_TODO = WM_APP + 27,          // tests: wp 0 = tasks | done << 8 | editor << 16 | reminder kind << 20 | edit mode << 24;
                                        //        1 = check reminders as if the time were lp (minutes since midnight); 2 = reminders waiting
    WM_APP_PET = WM_APP + 28,           // tests: wp 0 = mascot mode | percent shown << 4; 1 = appear now as if away 15 s (returns 1 if it did)
    WM_APP_CAL = WM_APP + 29,           // tests: wp 5 = event reminder check as if the minute were lp (1 if its pop-up shows); 6 = next due minute
                                        //        wp 0 = status | events << 4; 1 = items on day today + lp; 2 = open | week << 1 |
                                        //        (selected day - today + 1000) << 4; 3 = download again; 4 = month shown (y * 100 + m)
    WM_APP_CAL_DONE = WM_APP + 30,      // calendar download finished: lp = CalJob*
    WM_APP_TRAY = WM_APP + 31,          // the tray icon was clicked
    WM_APP_STRAY = WM_APP + 32,
    WM_APP_ASK_DONE = WM_APP + 35,      // an Ask job finished: lp = AskJob*
    WM_APP_ASK = WM_APP + 36,           // tests: wp 0 = open | busy << 1 | has key << 2 | error << 3 | answer length << 8;
                                        //        1 = open Ask; 2 = models known; 3 = close; 4 = save the key in global atom lp
    WM_APP_CLIP = WM_APP + 34,          // tests: wp 0 = items; 1 = show tab lp (0 Files, 1 Clipboard)
    WM_APP_WATER = WM_APP + 33,         // tests: wp 0 = today's ml; 1 = add lp ml (may be < 0); 2 = reminder check as if the minute were lp
                                        //        (returns 1 if the water pop-up shows); 3 = next reminder minute (-1: none)         // tests: pixels of the window that aren't clear, outside the area the notch drew last frame
};
enum SettingQuery { SQ_WIDTH = 2, SQ_HEIGHT, SQ_ROUNDNESS, SQ_LOADS = 6, SQ_STYLE, SQ_GLASS, SQ_SIZE, SQ_MINIMAL, SQ_CLOCK, SQ_RING,
                    SQ_SCALE_X1000 }; // tests: SQ_SCALE_X1000 = px per DIP x 1000 (DPI x size)
enum { TIMER_ANIM = 1, TIMER_CLOCK = 2, TIMER_FLASH = 3, TIMER_SETTINGS = 6, TIMER_MINUTE = 7, TIMER_CAPS = 8, TIMER_LANG = 9, TIMER_POMO = 10, TIMER_TODO = 11,
       TIMER_PET_IDLE = 12, TIMER_PET_ANIM = 13, // mascot: away 15 s / its frames
       TIMER_CAL = 14,                           // calendar: download again
       TIMER_PEEK = 15,                          // hover: open after the peek
       TIMER_WATER = 16,                         // water: the next reminder (or midnight)
       TIMER_CLIP = 17,                          // clipboard: "Copied" goes
       TIMER_CALREM = 18,                        // calendar: the next event reminder
       TIMER_ASKCOPY = 19 };                     // Ask: "Copied" goes
static const int kAskHotkeyId = 1;           // RegisterHotKey id for the Ask Perch shortcut                      // calendar: the next event reminder                        // clipboard: "Copied" goes                       // water: the next reminder (or midnight)                        // hover: open after the peek
enum { CMD_QUIT = 100, CMD_AUTOSTART = 101,
       CMD_MEDIA_PLAYPAUSE = 104, CMD_MEDIA_NEXT = 105, CMD_MEDIA_PREV = 106, CMD_SHELF_CLEAR = 107,
       CMD_SETTINGS = 109, CMD_POMO_TOGGLE = 110, CMD_POMO_RESET = 111,
       CMD_CAL_PASTE = 112, CMD_CAL_REFRESH = 113, CMD_CAL_REMOVE = 114, CMD_MINIMAL = 115, CMD_MASCOT = 116, CMD_ABOUT = 117, CMD_ASK = 118, CMD_ASK_KEY = 119,
       CMD_SIZE0 = 130, // + Notch size (Auto, 100%, 115%, 130%, 150%)
       CMD_TILE_OPEN = 120, CMD_TILE_SHOW = 121, CMD_TILE_REMOVE = 122 };
enum StateFlags {
    ST_EXPANDED = 1, ST_FULLSCREEN_HIDDEN = 32, ST_AUTOSTART = 64, ST_TOAST = 128,
    ST_HAS_MEDIA = 256, ST_PLAYING = 512, ST_ART = 1024, ST_HIDDEN = 4096, ST_HOVER = 16384,
};

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"Perch";

// ---------- state ----------
static HWND g_hwnd;
static float g_scale = 1.0f;             // DPI / 96
static int g_pxW, g_pxH;                  // window size in pixels
static float g_t = 0, g_target = 0;       // expansion 0..1
static bool g_hover = false, g_flash = false, g_animating = false;
static bool g_hoverOpen = false;          // the pointer has stayed long enough (the pill peeks first)
// Springs: open / close rise 10-90% in ~130 ms with ~2% overshoot.
static float g_tv = 0;                    // velocity of g_t
static float g_cw = 0, g_ch = 0, g_cwv = 0, g_chv = 0; // closed size, following CollapsedWidth / Height
static float g_peek = 0, g_peekv = 0, g_peekTarget = 0; // hover swell before opening (+10 wide, +4.8 tall)
static LARGE_INTEGER g_animLast;
static bool g_fsHidden = false;

// pop-up (volume, brightness, battery, output device...)
static bool g_toast = false;
static wchar_t g_toastMain[64], g_toastSub[96];
static const wchar_t* g_toastGlyph = nullptr; // icon in a circle on the left (Segoe Fluent Icons)
static float g_toastLevel = -1;               // 0..1 shows a level bar (volume, brightness...)
static bool g_toastLevelDim = false;          // grey bar (e.g. muted)
static bool g_toastLevelWarn = false;         // red bar (e.g. low battery)
static ATOM g_toastAtom;                      // toast text published for tests

static ID2D1Factory* g_d2d;
static IDWriteFactory* g_dw;
static ID2D1DCRenderTarget* g_rt;
static ID2D1SolidColorBrush* g_brush;
static IDWriteTextFormat *g_fmtTime, *g_fmtDate, *g_fmtIcon, *g_fmtSmall, *g_fmtPill, *g_fmtPillTime;
static IDWriteTextFormat *g_fmtTitle, *g_fmtArtist, *g_fmtCtl, *g_fmtMeta, *g_fmtTile, *g_fmtDrop;
static IDWriteTextFormat* g_fmtToast; // toast text too long for the big clock-size font
static IDWriteTextFormat* g_fmtAsk; // Ask: the answer, wrapped
static IDWriteTextFormat *g_fmtHdr, *g_fmtHdrDate, *g_fmtHdrTime, *g_fmtChevron, *g_fmtBarIcon, *g_fmtPlay; // pages
static IDWriteTextFormat *g_fmtTodoCount, *g_fmtTodoBtn, *g_fmtTodoRow, *g_fmtTodoTime, *g_fmtTodoSmall, *g_fmtTodoField; // To-do
static IDWriteTextFormat *g_fmtActTitle, *g_fmtActSub, *g_fmtActBtn; // reminder pop-up
static IDWriteTextFormat* g_fmtPetZ; // the mascot's "z"
static IDWriteTextFormat *g_fmtCalNum, *g_fmtCalNumB, *g_fmtCalWd, *g_fmtCalHead, *g_fmtCalWeekNum, *g_fmtCalLine; // calendar
static IDWriteTextFormat *g_fmtPomoTime, *g_fmtPomoSub, *g_fmtPomoBtn, *g_fmtPomoLabel, *g_fmtPomoState, *g_fmtPomoSmall, *g_fmtPomoFoot; // Pomodoro page

// shelf (5.1)
static const float kTileW = 60, kTileGap = 6, kIconSz = 40;
struct ShelfItem {
    std::wstring path, name;
    ID2D1Bitmap* icon = nullptr; // tied to the render target
    bool iconTried = false;
    bool selected = false;
};
static std::vector<ShelfItem> g_shelf;
static bool g_act = false;                  // a reminder pop-up with buttons is showing (stays until answered)
static const float kActW = 320.8f, kActH = 60, kActR = 22; // its size
static int g_page = PAGE_MUSIC;             // page the open notch shows (kept between opens)
static int g_pagePrev = PAGE_MUSIC;         // page sliding out
static float g_pageT = 1;                   // 0..1 slide from g_pagePrev to g_page
static int g_pageDir = 1;                   // +1 slides left (next), -1 slides right (previous)
static float g_iconBar = 0, g_iconBarTarget = 0; // 0..1, page icons in place of the dots
static int g_hot = -1;                      // hot spot under the pointer (HOT_*), for hover highlights
static bool g_glassOn = false;            // Glass: a blurred copy of what's behind sits under the tint
static ID2D1Bitmap* g_glassBmp = nullptr; // the blurred copy, a quarter of the window's size
static RECT g_glassAt = {};               // screen rectangle it was taken from
static bool g_dragOver = false;             // files are being dragged over the notch
static bool g_draggingOut = false;          // we're dragging shelf items out
static int g_pressTile = -1;                // item under a left-button press
static int g_pressClose = -1;               // item whose remove button is pressed
static int g_hoverClose = -1;               // item whose remove button is under the pointer
static POINT g_pressPt;
static int g_menuTile = -1;                 // item a tile context menu is for
static IWICImagingFactory* g_wic;
static ID2D1StrokeStyle *g_dash, *g_round; // dashed drop zone; round caps for ticks and icons

// settings (1.5)
static Settings g_set;                // values in use (from settings.ini, else defaults)
static FILETIME g_setStamp;           // settings.ini last-write time when last loaded
static int g_setLoads;                // how many times settings were loaded (tests)

// ---------- notch style and visibility ----------
static float g_offY = 0;             // Floating: gap above the pill, DIPs (snapped to whole pixels)
static D2D1_RECT_F g_pill;           // pill as last drawn, DIPs (before g_offY)
static bool g_hidden = false;        // window hidden (full screen app in front)

static float PillX(float w) { return (kWinW - w) / 2; } // the notch is always centred
static bool MinimalTab(); // Minimal notch and nothing playing: the small tab
static float CollapsedHeight() { return MinimalTab() ? 4.0f : (float)g_set.height; }

// now playing (3.1 / 3.2)
static MediaInfo g_media;

// Closed pill width.
// Minimal notch: a 44 x 4 tab with no clock (and no mascot) while nothing is playing.
static bool MinimalTab() { return g_set.minimal && !(g_media.hasSession && g_media.playing); }
static float CollapsedWidth() { return MinimalTab() ? 44.0f : (float)g_set.width; }
static float WantedScale() { return GetDpiForWindow(g_hwnd) / 96.0f * Settings_SizeFactor(g_set); } // DPI x the Notch size setting

// The closed pill's clock changes once a minute: one timer for the next minute boundary, re-armed when it fires.
static void ArmMinuteTimer() {
    if (!g_set.clock) { KillTimer(g_hwnd, TIMER_MINUTE); return; }
    SYSTEMTIME st;
    GetLocalTime(&st);
    UINT ms = (60 - st.wSecond) * 1000 - st.wMilliseconds + 50;
    SetTimer(g_hwnd, TIMER_MINUTE, ms, nullptr);
}
static ID2D1Bitmap* g_art;          // artwork bitmap (tied to the render target)
static unsigned g_artLoaded = ~0u;  // MediaInfo::artVersion that g_art was made from
static HDC g_memDC;
static ID2D1Bitmap* g_petGlow;          // the mascot's glow, drawn once per notch size (tied to the render target)
static float g_petGlowKey[5];
static float g_baseX = 0; // left edge (DIPs) of the area the render target is bound to this frame
static D2D1_RECT_F g_lastDrawn = {0, 0, 0, 0}; // what the last frame covered (DIPs), so the next one also erases it
static const DWORD* g_bits = nullptr;     // the window's pixels (premultiplied BGRA, top-down)
static bool g_fullUpdate = true;               // next frame redraws and sends the whole surface
static HBITMAP g_bmp, g_oldBmp;

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) { c.a *= a; return c; }

// ---------- colours: black, or Glass (dark tint over Windows' blur) ----------
static D2D1_COLOR_F g_bg = {0.02f, 0.02f, 0.02f, 1};     // pill
static D2D1_COLOR_F g_fg = {1, 1, 1, 1};                 // main text and icons
static D2D1_COLOR_F g_grey = {0.62f, 0.62f, 0.64f, 1};   // secondary text
static D2D1_COLOR_F g_chip = {0.17f, 0.17f, 0.18f, 1};   // round buttons and tiles
static D2D1_COLOR_F g_edge = {0, 0, 0, 0};               // thin outline (Glass)

static void SetGlassColours(bool glass) {
    if (glass) { g_bg = {0.05f, 0.05f, 0.07f, std::min(0.95f, std::max(0.05f, g_set.glassTint / 100.0f * 1.375f))}; /* Glass tint, 40% by default */ g_grey = {0.80f, 0.80f, 0.83f, 1}; g_chip = {1, 1, 1, 0.14f}; g_edge = {1, 1, 1, 0.22f}; }
    else { g_bg = {0, 0, 0, 1}; g_grey = {0.62f, 0.62f, 0.64f, 1}; g_chip = {0.17f, 0.17f, 0.18f, 1}; g_edge = {0, 0, 0, 0}; }
}

static void Render();
static void UpdateTarget();
static void KickAnim();
static void ShowToast(const wchar_t* main, const wchar_t* sub, const wchar_t* glyph = nullptr, float level = -1, bool levelDim = false);
static void PetRecheck();

// =====================================================================
// 3.3 Volume pop-up and 3.6 output-device (headphones) pop-up
// IAudioEndpointVolumeCallback on the default output tells us about volume
// and mute changes; IMMNotificationClient tells us when the default output
// changes (headphones plugged in, Bluetooth connected...). Both callbacks just
// post a message to the UI thread.
// =====================================================================
static IAudioEndpointVolume* g_outVol;
static bool g_outIsHeadphones = false;
static wchar_t g_outName[128] = L"";

class VolumeSink final : public IAudioEndpointVolumeCallback {
    LONG m_ref = 1;

public:
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        if (IsEqualIID(iid, kIID_IUnknown) || IsEqualIID(iid, kIID_IAudioEndpointVolumeCallback)) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA d) override {
        PostMessageW(g_hwnd, WM_APP_VOLUME, (WPARAM)lroundf(d->fMasterVolume * 1000.0f), d->bMuted ? 1 : 0);
        return S_OK;
    }
};

class DeviceSink final : public IMMNotificationClient {
    LONG m_ref = 1;

public:
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        if (IsEqualIID(iid, kIID_IUnknown) || IsEqualIID(iid, kIID_IMMNotificationClient)) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR id, DWORD state) override { // checked on the UI thread (Bluetooth?)
        if (state == DEVICE_STATE_ACTIVE && id) PostMessageW(g_hwnd, WM_APP_BT_DEVICE, 0, (LPARAM)_wcsdup(id));
        return S_OK;
    }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) PostMessageW(g_hwnd, WM_APP_OUTPUT_CHANGED, 0, 0);
        return S_OK;
    }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

// =====================================================================
// 3.5 Battery and 3.4 Brightness pop-ups
// Windows pushes power-setting changes to the window (WM_POWERBROADCAST);
// each setting also sends its current value right after registering,
// which we use as the starting point (no pop-up for it).
// =====================================================================
static int g_onAC = -1;       // 1 plugged in, 0 on battery, -1 unknown
static int g_battPct = -1;    // 0..100, -1 unknown / no battery
static int g_brightness = -1; // 0..100, -1 unknown
static HPOWERNOTIFY g_powerNotify[3];

static const wchar_t* BatteryGlyph(int pct, bool charging) {
    static wchar_t g[2];
    int step = pct < 0 ? 0 : (pct + 5) / 10;                     // 0..10
    g[0] = (wchar_t)((charging ? 0xE85B : 0xE850) + (step > 10 ? 10 : step)); // Battery0..10 / BatteryCharging0..10
    g[1] = 0;
    return g;
}

static void ShowBatteryToast(const wchar_t* main, const wchar_t* sub, bool charging) {
    if (!g_set.popBattery) return;
    ShowToast(main, sub, BatteryGlyph(g_battPct, charging), g_battPct < 0 ? 0 : g_battPct / 100.0f, false);
}

// One place for both real and simulated reports. Pass -1 for "unchanged".
static void OnPowerReport(int onAC, int pct) {
    int prevAC = g_onAC, prevPct = g_battPct;
    if (onAC >= 0) g_onAC = onAC;
    if (pct >= 0) g_battPct = pct;
    if (g_battPct < 0) return; // desktop PC without a battery: nothing to show

    wchar_t sub[64];
    if (onAC >= 0 && prevAC >= 0 && onAC != prevAC) {
        swprintf(sub, 64, L"%d%% %ls", g_battPct, onAC ? L"\x00B7 plugged in" : L"remaining");
        ShowBatteryToast(onAC ? L"Charging" : L"On battery", sub, onAC != 0);
    } else if (pct >= 0 && prevPct >= 0 && pct != prevPct) {
        if (g_onAC == 0 && ((prevPct > 20 && pct <= 20) || (prevPct > 10 && pct <= 10))) {
            swprintf(sub, 64, L"%d%% left \x00B7 plug in soon", pct);
            ShowBatteryToast(L"Low battery", sub, false);
            g_toastLevelWarn = true;
            Render();
        } else if (g_onAC == 1 && prevPct < 100 && pct == 100) {
            ShowBatteryToast(L"Fully charged", L"You can unplug", true);
        }
    }
    if ((g_onAC != prevAC || g_battPct != prevPct) && !g_hidden) Render(); // the battery ring follows every change
}

static void OnBrightnessReport(int level) {
    int prev = g_brightness;
    g_brightness = level;
    if (prev < 0 || level == prev || !g_set.popBrightness) return; // first report is just the starting value
    ShowToast(L"Brightness", L"Built-in display", L"\xE706", level / 100.0f, false);
}

static void RegisterPowerNotifications() {
    const GUID* ids[3] = {&kGUID_ACDC_POWER_SOURCE, &kGUID_BATTERY_PERCENTAGE_REMAINING, &kGUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS};
    for (int i = 0; i < 3; i++) g_powerNotify[i] = RegisterPowerSettingNotification(g_hwnd, ids[i], DEVICE_NOTIFY_WINDOW_HANDLE);
}

static void UnregisterPowerNotifications() {
    for (HPOWERNOTIFY& h : g_powerNotify)
        if (h) { UnregisterPowerSettingNotification(h); h = nullptr; }
}

static void OnPowerSettingChange(const POWERBROADCAST_SETTING* s) {
    if (s->DataLength < sizeof(DWORD)) return;
    DWORD v = *(const DWORD*)s->Data;
    if (IsEqualGUID(s->PowerSetting, kGUID_ACDC_POWER_SOURCE)) OnPowerReport(v == 0 ? 1 : 0, -1); // 0 = AC, 1 = battery, 2 = UPS
    else if (IsEqualGUID(s->PowerSetting, kGUID_BATTERY_PERCENTAGE_REMAINING)) OnPowerReport(-1, (int)v);
    else if (IsEqualGUID(s->PowerSetting, kGUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS)) OnBrightnessReport((int)v);
}

static VolumeSink* g_volSink;
static DeviceSink* g_devSink;
static IMMDeviceEnumerator* g_devEnum;

// (Re)attach to the current default output device. Returns its volume (0..1).
static float AttachDefaultOutput() {
    if (g_outVol) {
        g_outVol->UnregisterControlChangeNotify(g_volSink);
        SafeRelease(g_outVol);
    }
    g_outName[0] = 0;
    g_outIsHeadphones = false;
    float level = -1;
    IMMDevice* dev = nullptr;
    if (!g_devEnum || FAILED(g_devEnum->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return level;
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
            wcsncpy(g_outName, v.pwszVal, 127);
            g_outName[127] = 0;
        }
        PropVariantClear(&v);
        if (SUCCEEDED(ps->GetValue(kPKEY_AudioEndpoint_FormFactor, &v)) && v.vt == VT_UI4)
            g_outIsHeadphones = v.ulVal == 3 /*Headphones*/ || v.ulVal == 5 /*Headset*/;
        PropVariantClear(&v);
        ps->Release();
    }
    if (SUCCEEDED(dev->Activate(kIID_IAudioEndpointVolume, CLSCTX_ALL, nullptr, (void**)&g_outVol))) {
        g_outVol->RegisterControlChangeNotify(g_volSink);
        g_outVol->GetMasterVolumeLevelScalar(&level);
    }
    dev->Release();
    return level;
}

static void StartAudioNotifications() {
    g_volSink = new VolumeSink();
    g_devSink = new DeviceSink();
    if (SUCCEEDED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&g_devEnum)))
        g_devEnum->RegisterEndpointNotificationCallback(g_devSink);
    AttachDefaultOutput();
}

// =====================================================================
// Indicators and pop-ups: Caps Lock, input language, Wi-Fi, Bluetooth headset.
// A low-level keyboard hook only posts for Caps Lock and for keys that switch the
// language (Win+Space, Alt+Shift, Ctrl+Shift); everything else passes straight on.
// Wi-Fi comes from the WLAN service's own notifications; Bluetooth from the audio
// device listener above. Nothing polls.
// =====================================================================
static const PROPERTYKEY kPKEY_Device_EnumeratorName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 24};
static const GUID kIID_IMMEndpoint = {0x1BE09788, 0x6894, 0x4089, {0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5}};
static HHOOK g_kbHook;
static bool g_capsDown;
static HKL g_lastHkl;
static HANDLE g_wlan;
static ULONGLONG g_btShownAt; // the output-device pop-up stays quiet right after a Bluetooth one
static wchar_t g_btLast[128];

static LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
        bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        switch (k->vkCode) {
        case VK_CAPITAL:
            if (down && !g_capsDown) PostMessageW(g_hwnd, WM_APP_KEY, VK_CAPITAL, 0); // not for auto-repeat
            g_capsDown = down;
            break;
        case VK_SPACE: case VK_LSHIFT: case VK_RSHIFT: case VK_LMENU: case VK_RMENU: case VK_LCONTROL: case VK_RCONTROL:
        case VK_LWIN: case VK_RWIN:
            if (!down) PostMessageW(g_hwnd, WM_APP_KEY, k->vkCode, 0);
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static HKL ForegroundLayout() {
    HWND fg = GetForegroundWindow();
    return GetKeyboardLayout(fg ? GetWindowThreadProcessId(fg, nullptr) : 0);
}

static void UpdateKeyboardHook() { // only while Caps Lock or input language pop-ups are on
    bool want = g_set.popCaps || g_set.popLanguage;
    if (want && !g_kbHook) {
        g_lastHkl = ForegroundLayout();
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, GetModuleHandleW(nullptr), 0);
    } else if (!want && g_kbHook) {
        UnhookWindowsHookEx(g_kbHook);
        g_kbHook = nullptr;
    }
}

static void ShowCapsToast() {
    bool on = (GetKeyState(VK_CAPITAL) & 1) != 0;
    ShowToast(L"Caps Lock", on ? L"On" : L"Off", L"\xE765"); // Keyboard
}

static void ShowLanguageToast(LANGID lang) {
    wchar_t loc[LOCALE_NAME_MAX_LENGTH] = L"", name[128] = L"", code[16] = L"";
    LCIDToLocaleName(MAKELCID(lang, SORT_DEFAULT), loc, LOCALE_NAME_MAX_LENGTH, 0);
    if (!GetLocaleInfoEx(loc, LOCALE_SLOCALIZEDDISPLAYNAME, name, 128)) wcscpy(name, loc);
    GetLocaleInfoEx(loc, LOCALE_SISO639LANGNAME2, code, 16); // "eng", like the taskbar's "ENG"
    for (wchar_t* c = code; *c; c++) *c = towupper(*c);
    wchar_t sub[64];
    swprintf(sub, 64, L"Input language \x00B7 %ls", code);
    ShowToast(name, sub, L"\xE765");
}

static void CheckInputLanguage() {
    HKL hkl = ForegroundLayout();
    if (hkl == g_lastHkl) return;
    g_lastHkl = hkl;
    if (g_set.popLanguage) ShowLanguageToast(LOWORD((ULONG_PTR)hkl));
}

// An audio output became active: a pop-up if it is a Bluetooth headset or speaker.
static void OnAudioDeviceActive(wchar_t* id) {
    wchar_t name[128] = L"Test Headset";
    bool bt = id == nullptr; // tests send no id
    IMMDevice* dev = nullptr;
    if (id && g_devEnum && SUCCEEDED(g_devEnum->GetDevice(id, &dev))) {
        IMMEndpoint* ep = nullptr;
        EDataFlow flow = eCapture;
        if (SUCCEEDED(dev->QueryInterface(kIID_IMMEndpoint, (void**)&ep))) { ep->GetDataFlow(&flow); ep->Release(); }
        IPropertyStore* ps = nullptr;
        if (flow == eRender && SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(kPKEY_Device_EnumeratorName, &v)) && v.vt == VT_LPWSTR) bt = _wcsnicmp(v.pwszVal, L"BTH", 3) == 0;
            PropVariantClear(&v);
            if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) { wcsncpy(name, v.pwszVal, 127); name[127] = 0; }
            PropVariantClear(&v);
            ps->Release();
        }
        dev->Release();
    }
    free(id);
    // A headset brings up two outputs (stereo and hands-free) at once: one pop-up.
    bool again = GetTickCount64() - g_btShownAt < 3000 && !wcscmp(name, g_btLast);
    if (!bt || again || !g_set.popBluetooth) return;
    wcscpy(g_btLast, name);
    g_btShownAt = GetTickCount64();
    ShowToast(name, L"Connected", L"\xE7F6"); // Headphone
}

static int CountBluetoothOutputs() {
    int n = 0;
    IMMDeviceCollection* all = nullptr;
    if (!g_devEnum || FAILED(g_devEnum->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, &all))) return -1;
    UINT count = 0;
    all->GetCount(&count);
    for (UINT i = 0; i < count; i++) {
        IMMDevice* dev = nullptr;
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(all->Item(i, &dev)) && SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(kPKEY_Device_EnumeratorName, &v)) && v.vt == VT_LPWSTR && !_wcsnicmp(v.pwszVal, L"BTH", 3)) n++;
            PropVariantClear(&v);
            ps->Release();
        }
        SafeRelease(dev);
    }
    all->Release();
    return n;
}

static void WINAPI WlanCallback(PWLAN_NOTIFICATION_DATA d, PVOID) { // WLAN service thread
    if (d->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM || !d->pData) return;
    bool connected = d->NotificationCode == wlan_notification_acm_connection_complete;
    if (!connected && d->NotificationCode != wlan_notification_acm_disconnected) return;
    const WLAN_CONNECTION_NOTIFICATION_DATA* c = (const WLAN_CONNECTION_NOTIFICATION_DATA*)d->pData;
    if (connected && c->wlanReasonCode != WLAN_REASON_CODE_SUCCESS) return; // a failed attempt
    wchar_t ssid[64] = L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)c->dot11Ssid.ucSSID, (int)c->dot11Ssid.uSSIDLength, ssid, 63);
    ssid[n > 0 ? n : 0] = 0;
    PostMessageW(g_hwnd, WM_APP_WIFI, connected ? 1 : 0, (LPARAM)_wcsdup(ssid[0] ? ssid : c->strProfileName));
}

static void OnWifi(bool connected, wchar_t* ssid) {
    if (g_set.popWifi) ShowToast(connected ? L"Connected" : L"Disconnected", ssid ? ssid : L"Test network",
                                 L"\xE701"); // Wifi
    free(ssid);
}

static void StartIndicators() {
    UpdateKeyboardHook();
    DWORD ver = 0;
    if (WlanOpenHandle(2, nullptr, &ver, &g_wlan) == ERROR_SUCCESS) // no Wi-Fi adapter or service: nothing to do
        WlanRegisterNotification(g_wlan, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, WlanCallback, nullptr, nullptr, nullptr);
    else g_wlan = nullptr;
}

static void StopIndicators() {
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
    if (g_wlan) { WlanRegisterNotification(g_wlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr); WlanCloseHandle(g_wlan, nullptr); g_wlan = nullptr; }
}

static void StopAudioNotifications() {
    if (g_outVol) { g_outVol->UnregisterControlChangeNotify(g_volSink); SafeRelease(g_outVol); }
    if (g_devEnum) { g_devEnum->UnregisterEndpointNotificationCallback(g_devSink); SafeRelease(g_devEnum); }
    SafeRelease(g_volSink);
    SafeRelease(g_devSink);
}

// =====================================================================
// 1.4 Start with Windows
// =====================================================================
static bool AutostartEnabled() {
    DWORD sz = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, &sz) == ERROR_SUCCESS;
}

static void SetAutostart(bool on) {
    if (on) {
        wchar_t exe[MAX_PATH], val[MAX_PATH + 2];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        swprintf(val, MAX_PATH + 2, L"\"%ls\"", exe);
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    }
}

// =====================================================================
// 4.1 Fullscreen: the shell tells registered appbars when a fullscreen
// app opens/closes. We register as an appbar without reserving space.
// =====================================================================
// The window hides while a full screen app is in front ("Step aside for full screen").
static void UpdateVisibility() {
    bool hide = g_fsHidden;
    if (hide == g_hidden) return;
    g_hidden = hide;
    if (hide) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        Render();
        PetRecheck();
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void SetFullscreenHidden(bool hide) {
    g_fsHidden = hide;
    UpdateVisibility();
}

static bool ForegroundIsFullscreenOnPrimary() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_hwnd || fg == GetDesktopWindow() || fg == GetShellWindow()) return false;
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL);
    if (mon != MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY)) return false;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    RECT r;
    GetWindowRect(fg, &r);
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

// =====================================================================
// Graphics
// =====================================================================
static bool HasFont(const wchar_t* family) {
    IDWriteFontCollection* fc = nullptr;
    bool found = false;
    if (SUCCEEDED(g_dw->GetSystemFontCollection(&fc, FALSE))) {
        UINT32 idx;
        BOOL exists = FALSE;
        if (SUCCEEDED(fc->FindFamilyName(family, &idx, &exists))) found = exists != FALSE;
        fc->Release();
    }
    return found;
}

static IDWriteTextFormat* MakeFormat(const wchar_t* family, DWRITE_FONT_WEIGHT w, float size, bool centered) {
    IDWriteTextFormat* f = nullptr;
    g_dw->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (centered) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return f;
}

static void CreateFormats() {
    const wchar_t* iconFont = HasFont(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    g_fmtTime = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 36, false);
    g_fmtDate = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 14, false);
    g_fmtIcon = MakeFormat(iconFont, DWRITE_FONT_WEIGHT_NORMAL, 20, true);
    g_fmtSmall = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 10.5f, true);
    g_fmtPill = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f, false);
    g_fmtPill->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtPill->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    g_fmtPillTime = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 16, true); // closed pill clock (bold, centred)

    g_fmtTitle = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 16, false);
    g_fmtToast = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 22, false);
    g_fmtArtist = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13, false);
    g_fmtMeta = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12, false);
    g_fmtTile = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11, true);
    g_fmtDrop = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 15, true);
    g_fmtCtl = MakeFormat(iconFont, DWRITE_FONT_WEIGHT_NORMAL, 15, true);
    g_fmtHdr = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 15, false);
    g_fmtAsk = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12, false);
    if (g_fmtAsk) { g_fmtAsk->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); g_fmtAsk->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); }
    g_fmtHdr->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtHdrDate = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13, false);
    g_fmtHdrDate->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtHdrTime = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 13, false);
    g_fmtHdrTime->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtChevron = MakeFormat(iconFont, DWRITE_FONT_WEIGHT_NORMAL, 10, true);
    g_fmtBarIcon = MakeFormat(iconFont, DWRITE_FONT_WEIGHT_NORMAL, 13, true);
    g_fmtPlay = MakeFormat(iconFont, DWRITE_FONT_WEIGHT_NORMAL, 19, true);
    g_fmtPomoTime = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 28, false);
    g_fmtPomoTime->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtPomoSub = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 13.5f, false);
    g_fmtPomoSub->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtPomoBtn = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 10.5f, true);
    g_fmtPomoLabel = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 9.5f, true);
    g_fmtPomoState = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 11.5f, false); // "Ready" / "Paused"
    g_fmtPomoState->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtPomoSmall = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 10.5f, false); // "Focus Â· 1 of 4", "This round"
    g_fmtPomoSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtPomoFoot = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 9.5f, false); // "2 h 10 min Â· 4 sessions"
    g_fmtPomoFoot->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtTodoCount = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 36, false);
    g_fmtTodoBtn = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 10, false); // "Add Task"
    g_fmtTodoRow = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, false);
    g_fmtTodoTime = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 10.5f, false);
    g_fmtTodoField = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, false);
    g_fmtActTitle = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 14.2f, false);
    g_fmtActSub = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11, false);
    for (IDWriteTextFormat* f : {g_fmtTodoCount, g_fmtTodoBtn, g_fmtTodoRow, g_fmtTodoTime, g_fmtTodoField, g_fmtActTitle, g_fmtActSub})
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtTodoSmall = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, true);
    g_fmtActBtn = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 11.5f, true);
    g_fmtPetZ = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 8, true);
    g_fmtCalNum = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 8.5f, true);
    g_fmtCalNumB = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 8.5f, true);
    g_fmtCalWd = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 7.5f, true);
    g_fmtCalWeekNum = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 11, true);
    g_fmtCalHead = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 10.5f, false);
    g_fmtCalHead->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_fmtCalLine = MakeFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 10.5f, false);
    g_fmtCalLine->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Long titles end in "â€¦" instead of overflowing.
    IDWriteInlineObject* ellipsis = nullptr;
    DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    for (IDWriteTextFormat* f : {g_fmtTitle, g_fmtArtist, g_fmtTile, g_fmtTodoRow, g_fmtActTitle, g_fmtActSub, g_fmtCalLine}) {
        if (SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(f, &ellipsis))) {
            f->SetTrimming(&trim, ellipsis);
            ellipsis->Release();
        }
    }
}

static void ReleaseSurface() {
    for (ShelfItem& it : g_shelf) { SafeRelease(it.icon); it.iconTried = false; }
    SafeRelease(g_art);
    SafeRelease(g_petGlow);
    SafeRelease(g_glassBmp); // belongs to the old surface; the next copy is made on the new one
    g_artLoaded = ~0u; // recreate artwork on the new surface
    SafeRelease(g_brush);
    SafeRelease(g_rt);
    if (g_memDC) {
        SelectObject(g_memDC, g_oldBmp);
        DeleteObject(g_bmp);
        DeleteDC(g_memDC);
        g_memDC = nullptr;
    }
}

static bool CreateSurface() {
    ReleaseSurface();
    g_fullUpdate = true;
    g_pxW = (int)std::ceil(kWinW * g_scale);
    g_pxH = (int)std::ceil(kWinH * g_scale);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = g_pxW;
    bi.bmiHeader.biHeight = -g_pxH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    g_bits = nullptr;
    g_memDC = CreateCompatibleDC(screen);
    g_bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!g_memDC || !g_bmp) return false;
    g_bits = (const DWORD*)bits;
    g_oldBmp = (HBITMAP)SelectObject(g_memDC, g_bmp);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, // avoids loading the GPU driver (~tens of MB) for a tiny surface
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * g_scale, 96.0f * g_scale);
    if (FAILED(g_d2d->CreateDCRenderTarget(&props, &g_rt))) return false;
    RECT rc = {0, 0, g_pxW, g_pxH};
    g_rt->BindDC(g_memDC, &rc);
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // ClearType needs an opaque background
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &g_brush);
    return true;
}

static void PlaceWindow() {
    g_fullUpdate = true;
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    g_offY = g_set.style == STYLE_ISLAND ? std::round(8 * g_scale) / g_scale : 0;
    int left = (mi.rcMonitor.left + mi.rcMonitor.right) / 2 - g_pxW / 2;
    SetWindowPos(g_hwnd, HWND_TOPMOST, left, mi.rcMonitor.top, g_pxW, g_pxH, SWP_NOACTIVATE);
    SafeRelease(g_glassBmp); // a copy taken at the old place doesn't fit the new one
}

// ---- open notch layout (DIPs; x measured from the open pill's left edge, px) ----
// The open notch's layout: header row at y 27, page body 52-150,
// arrows at the sides, dots (or page icons on hover) along the bottom.
enum Hot { HOT_NONE = -1, HOT_ARROW_L = 0, HOT_ARROW_R, HOT_DOTS, HOT_LYRICS, HOT_PREV, HOT_PLAY, HOT_NEXT, HOT_VOLUME,
           HOT_SHELF_CLEAR, HOT_ICON_SETTINGS = 10, HOT_ICON0, // HOT_ICON0 + page = that page's icon
           HOT_POMO_START = 40, HOT_POMO_RESET,
           HOT_TODO_ADD = 42, HOT_TODO_EDIT, HOT_TODO_CAL, HOT_TODO_TITLE, HOT_TODO_TIME, HOT_TODO_DAILY, HOT_TODO_CANCEL,
           HOT_TODO_SAVE, HOT_TODO_CLEAR, HOT_ACT_A, HOT_ACT_B, // HOT_ACT_*: buttons on a reminder pop-up
           HOT_CAL_PREV = 53, HOT_CAL_NEXT, HOT_CAL_TITLE, // calendar: previous / next month (week), Month <-> Week
           HOT_WATER_ADD = 56, HOT_WATER_UNDO,             // water page: + a cup, take one back
           HOT_SHELF_TAB0 = 58, HOT_CLIP_CLEAR = 9,         // Shelf: Files / Clipboard tabs (+ tab); Clear the clipboard list
           HOT_CLIP_ROW0 = 280, HOT_CLIP_DEL0 = 290,        // clipboard rows: copy, remove (+ row)
           HOT_ASK_COPY = 275, HOT_ASK_SETTINGS = 276,      // Ask: Copy answer; Open Settings (no key yet)
           HOT_TODO_ROW0 = 60, HOT_TODO_UP0 = 100, HOT_TODO_DOWN0 = 140, HOT_TODO_DEL0 = 180, HOT_TODO_MAX = 220, // + row
           HOT_CAL_DAY0 = 230, HOT_CAL_DAYMAX = 272 }; // + calendar cell
static D2D1_POINT_2F TodoHotCenter(int hot); // To-do page and reminder pop-up controls
static float PX() { return (kWinW - kExpW) / 2; }
static const int kClipRows = 3;
// layout: a small Files / Clipboard switch sitting on the top edge of the shelf's zone, three rows under it
static D2D1_RECT_F ShelfTabRect(int tab) { float cx = kWinW / 2; return tab ? D2D1::RectF(cx - 8, 48, cx + 66, 64) : D2D1::RectF(cx - 64, 48, cx - 12, 64); }
static D2D1_RECT_F ClipClearRect() { float right = PX() + kExpW - 40; return D2D1::RectF(right - 44, 48, right, 64); }
static D2D1_RECT_F ClipRowRect(int row) { float px = PX(); return D2D1::RectF(px + 32, 70 + row * 26.0f, px + kExpW - 32, 94 + row * 26.0f); }
static D2D1_POINT_2F ClipDelCenter(int row) { D2D1_RECT_F r = ClipRowRect(row); return D2D1::Point2F(r.right - 13, (r.top + r.bottom) / 2); }
static D2D1_RECT_F AskCopyRect();
static D2D1_POINT_2F HotCenter(int hot) {
    float px = PX(), cx = kWinW / 2;
    switch (hot) {
    case HOT_ARROW_L: return D2D1::Point2F(px + 11, kArrowY);
    case HOT_ARROW_R: return D2D1::Point2F(px + kExpW - 11, kArrowY);
    case HOT_LYRICS: return D2D1::Point2F(px + 62, 137);
    case HOT_PREV: return D2D1::Point2F(px + 150, 137);
    case HOT_PLAY: return D2D1::Point2F(px + 198, 137);
    case HOT_NEXT: return D2D1::Point2F(px + 244, 137);
    case HOT_VOLUME: return D2D1::Point2F(px + 352, 137);
    case HOT_POMO_START: return D2D1::Point2F(px + 310, 64);
    case HOT_POMO_RESET: return D2D1::Point2F(px + 355, 63);
    case HOT_WATER_ADD: return D2D1::Point2F(px + 245, 105);
    case HOT_WATER_UNDO: return D2D1::Point2F(px + 322, 105);
    }
    if (hot == HOT_SHELF_TAB0 || hot == HOT_SHELF_TAB0 + 1) { D2D1_RECT_F r = ShelfTabRect(hot - HOT_SHELF_TAB0); return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2); }
    if (hot == HOT_ASK_COPY || hot == HOT_ASK_SETTINGS) { D2D1_RECT_F r = AskCopyRect(); return D2D1::Point2F(r.right - 20, (r.top + r.bottom) / 2); }
    if (hot == HOT_CLIP_CLEAR) { D2D1_RECT_F r = ClipClearRect(); return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2); }
    if (hot >= HOT_CLIP_ROW0 && hot < HOT_CLIP_ROW0 + kClipRows) { D2D1_RECT_F r = ClipRowRect(hot - HOT_CLIP_ROW0); return D2D1::Point2F(r.left + 60, (r.top + r.bottom) / 2); }
    if (hot >= HOT_CLIP_DEL0 && hot < HOT_CLIP_DEL0 + kClipRows) return ClipDelCenter(hot - HOT_CLIP_DEL0);
    if ((hot >= HOT_TODO_ADD && hot < HOT_TODO_MAX) || (hot >= HOT_CAL_DAY0 && hot < HOT_CAL_DAYMAX)) return TodoHotCenter(hot);
    if (hot >= HOT_ICON0 || hot == HOT_ICON_SETTINGS) { // icon bar: page icons, a divider, then settings
        int n = g_pageN + 1, i = hot == HOT_ICON_SETTINGS ? g_pageN : hot - HOT_ICON0; // HOT_ICON0 + slot
        float step = 28, w = step * (n - 1) + 8; // icons 28 apart, the gear 36 after the last page; centred
        return D2D1::Point2F(cx - w / 2 + i * step + (i == g_pageN ? 8 : 0), kDotsY + 15.5f);
    }
    return D2D1::Point2F(-100, -100);
}
static D2D1_RECT_F WaterAddRect() { float px = PX(); return D2D1::RectF(px + 194, 92, px + 296, 118); }
static float HotRadius(int hot) { return hot >= HOT_CLIP_DEL0 && hot < HOT_CLIP_DEL0 + kClipRows ? 8 : hot == HOT_POMO_RESET || hot == HOT_WATER_UNDO ? 11 : hot == HOT_ARROW_L || hot == HOT_ARROW_R ? 12 : hot >= HOT_ICON_SETTINGS ? 12 : kCtlR; }
static D2D1_RECT_F DotsRect() { // area that reveals the page icons
    float cx = kWinW / 2, w = 28.0f * g_pageN + 36 + 36;
    return D2D1::RectF(cx - w / 2, kDotsY - 8, cx + w / 2, kDotsY + 8 + 30 * g_iconBar);
}

// Builds the Direct2D bitmap for the current artwork when it changed.
static void EnsureArt() {
    if (g_artLoaded == g_media.artVersion) return;
    SafeRelease(g_art);
    g_artLoaded = g_media.artVersion;
    if (g_media.art.empty() || !g_rt) return;
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_rt->CreateBitmap(D2D1::SizeU(g_media.artSize, g_media.artSize), g_media.art.data(), g_media.artSize * 4, &bp, &g_art);
}

// Artwork clipped to a rounded square (falls back to a music-note tile).
static void DrawArt(D2D1_RECT_F r, float radius, float alpha) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
    if (g_art) {
        ID2D1BitmapBrush* bb = nullptr;
        if (SUCCEEDED(g_rt->CreateBitmapBrush(g_art, &bb))) {
            float s = (r.right - r.left) / g_media.artSize;
            bb->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(r.left, r.top));
            bb->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            bb->SetOpacity(alpha);
            g_rt->FillRoundedRectangle(rr, bb);
            bb->Release();
            return;
        }
    }
    g_brush->SetColor(WithAlpha(g_chip, alpha));
    g_rt->FillRoundedRectangle(rr, g_brush);
    g_brush->SetColor(WithAlpha(g_fg, alpha * 0.7f));
    g_rt->DrawText(L"\xEC4F", 1, (r.right - r.left) > 30 ? g_fmtIcon : g_fmtSmall, r, g_brush); // MusicNote
}

// =====================================================================
// 5.1 Shelf: files dropped on the notch wait here until dragged out.
// Only paths are kept (nothing is copied); the list survives restarts in
// %APPDATA%\Perch\shelf.txt (or %PERCH_DATA_DIR%, used by tests).
// =====================================================================
static std::wstring DataDir() {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(L"PERCH_DATA_DIR", buf, 1024);
    std::wstring dir;
    if (n && n < 1024) {
        dir = buf;
    } else {
        n = GetEnvironmentVariableW(L"APPDATA", buf, 1024);
        dir = std::wstring(n && n < 1024 ? buf : L".") + L"\\Perch";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static void SaveShelf() {
    std::wstring s;
    for (const ShelfItem& it : g_shelf) { s += it.path; s += L"\n"; }
    std::wstring file = DataDir() + L"\\shelf.txt";
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WORD bom = 0xFEFF;
    WriteFile(f, &bom, 2, &written, nullptr);
    WriteFile(f, s.data(), (DWORD)(s.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
}

static bool ShelfAdd(const std::wstring& path) {
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    for (const ShelfItem& it : g_shelf)
        if (_wcsicmp(it.path.c_str(), path.c_str()) == 0) return false; // already there
    ShelfItem it;
    it.path = path;
    size_t slash = path.find_last_of(L"\\/");
    it.name = (slash == std::wstring::npos || slash + 1 == path.size()) ? path : path.substr(slash + 1);
    g_shelf.push_back(it);
    return true;
}

static void LoadShelf() {
    std::wstring file = DataDir() + L"\\shelf.txt";
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, nullptr), read = 0;
    std::wstring s;
    if (size >= 2 && size < (1u << 24)) {
        s.resize(size / sizeof(wchar_t));
        ReadFile(f, &s[0], size, &read, nullptr);
    }
    CloseHandle(f);
    if (!s.empty() && s[0] == 0xFEFF) s.erase(0, 1);
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(L'\n', start);
        if (end == std::wstring::npos) end = s.size();
        ShelfAdd(s.substr(start, end - start)); // files deleted meanwhile are dropped
        start = end + 1;
    }
}

static void ShelfRemove(int i) {
    if (i < 0 || i >= (int)g_shelf.size()) return;
    SafeRelease(g_shelf[i].icon);
    g_shelf.erase(g_shelf.begin() + i);
}

static void ShelfChanged() {
    SaveShelf();
    KickAnim();
    Render();
}

// ---- geometry (DIPs, expanded layout) ----
static D2D1_RECT_F ShelfZone() { return D2D1::RectF(PX() + 32, 58, PX() + kExpW - 32, 147); } // the drop zone
static int ShelfSlots() { return g_shelf.size() <= 5 ? (int)g_shelf.size() : 5; } // slot 4 becomes "+N" when > 5
static bool ShelfOverflowSlot(int slot) { return g_shelf.size() > 5 && slot == 4; }
static D2D1_RECT_F ShelfTileRect(int slot) {
    float x = ShelfZone().left + 8 + slot * (kTileW + kTileGap), y = 64;
    return D2D1::RectF(x, y, x + kTileW, y + 60);
}
static D2D1_RECT_F ShelfClearRect() {
    float right = ShelfZone().right - 10;
    return D2D1::RectF(right - 60, 128, right, 144);
}
static int g_shelfTab;
static int g_shelfTabShown() { return g_set.clipHistory ? g_shelfTab : 0; }
static bool ShelfInteractive() { return g_t >= 0.99f && g_page == PAGE_SHELF && g_pageT >= 0.99f && !g_shelf.empty() && !g_toast && g_shelfTabShown() == 0; }
// Remove button: a small round "x" on the top-right corner of each tile's icon.
static const float kCloseR = 8;
static D2D1_POINT_2F ShelfCloseCenter(int slot) {
    D2D1_RECT_F r = ShelfTileRect(slot);
    return D2D1::Point2F(r.left + (kTileW + kIconSz) / 2 - 2, r.top + 4);
}
static int ShelfHitClose(float x, float y) { // item index, or -1
    if (!ShelfInteractive()) return -1;
    for (int s = 0; s < ShelfSlots(); s++) {
        if (ShelfOverflowSlot(s)) continue;
        D2D1_POINT_2F c = ShelfCloseCenter(s);
        float hr = kCloseR + 2; // a little extra hit area
        if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= hr * hr) return s;
    }
    return -1;
}
// Item index under a point (DIPs), or -1.
static int ShelfHitTile(float x, float y) {
    if (!ShelfInteractive()) return -1;
    for (int s = 0; s < ShelfSlots(); s++) {
        D2D1_RECT_F r = ShelfTileRect(s);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return ShelfOverflowSlot(s) ? -1 : s;
    }
    return -1;
}

// File icon or thumbnail (photos, videos...) from the shell, as a D2D bitmap.
static ID2D1Bitmap* LoadShelfIcon(const std::wstring& path) {
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, kIID_IShellItemImageFactory, (void**)&factory))) return nullptr;
    int px = (int)std::ceil(kIconSz * g_scale);
    SIZE sz = {px, px};
    HBITMAP hb = nullptr;
    HRESULT hr = factory->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hb);
    factory->Release();
    if (FAILED(hr) || !hb) return nullptr;
    if (!g_wic) CoCreateInstance(kCLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, kIID_IWICImagingFactory, (void**)&g_wic);
    ID2D1Bitmap* bmp = nullptr;
    IWICBitmap* wb = nullptr;
    if (g_wic && SUCCEEDED(g_wic->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapUsePremultipliedAlpha, &wb))) {
        g_rt->CreateBitmapFromWicBitmap(wb, nullptr, &bmp);
        wb->Release();
    }
    DeleteObject(hb);
    return bmp;
}

static void EnsureShelfIcons() {
    if (!g_rt || g_page != PAGE_SHELF || g_t < 0.5f) return; // only load once the Shelf page is about to show
    for (int s = 0; s < ShelfSlots(); s++) {
        if (ShelfOverflowSlot(s)) continue;
        ShelfItem& it = g_shelf[s];
        if (!it.iconTried) { it.iconTried = true; it.icon = LoadShelfIcon(it.path); }
    }
}

static void Text(const wchar_t* s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c);
static void DrawTrayIcon(D2D1_POINT_2F c, float s, D2D1_COLOR_F col, D2D1_COLOR_F behind);

// Shelf page: a dashed drop zone. Empty: tray icon + "Drop files here"; while files are
// dragged over: "Release to add"; with items: the tiles sit inside the zone, links along its foot.
static void DrawShelf(float a) {
    D2D1_COLOR_F grey = WithAlpha(g_grey, a);
    D2D1_RECT_F zone = ShelfZone();
    bool empty = g_shelf.empty() || g_dragOver;
    g_brush->SetColor(g_dragOver ? D2D1::ColorF(0.45f, 0.65f, 1.0f, 0.9f * a) : WithAlpha(g_fg, 0.28f * a));
    if (g_dragOver) {
        g_brush->SetColor(D2D1::ColorF(0.25f, 0.52f, 1.0f, 0.10f * a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(zone, 14, 14), g_brush);
        g_brush->SetColor(D2D1::ColorF(0.45f, 0.65f, 1.0f, 0.9f * a));
    }
    if (empty) g_rt->DrawRoundedRectangle(D2D1::RoundedRect(zone, 14, 14), g_brush, 1.2f, g_dash);
    else {
        g_brush->SetColor(WithAlpha(g_fg, 0.05f * a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(zone, 14, 14), g_brush);
    }
    if (empty) {
        DrawTrayIcon(D2D1::Point2F((zone.left + zone.right) / 2, zone.top + 25), 20, WithAlpha(g_fg, 0.85f * a), g_bg);
        Text(g_dragOver ? L"Release to add" : L"Drop files here", g_fmtDrop, D2D1::RectF(zone.left, zone.top + 44, zone.right, zone.top + 66), WithAlpha(g_fg, a));
        if (!g_dragOver)
            Text(L"Drag out anytime \x00B7 click to open or copy", g_fmtTile, D2D1::RectF(zone.left, zone.top + 64, zone.right, zone.top + 80), grey);
        return;
    }
    wchar_t count[32];
    swprintf(count, 32, L"%d item%ls", (int)g_shelf.size(), g_shelf.size() == 1 ? L"" : L"s");
    Text(count, g_fmtMeta, D2D1::RectF(zone.left + 10, 128, zone.left + 120, 144), grey);
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    Text(L"Clear all", g_fmtMeta, ShelfClearRect(), WithAlpha(g_hot == HOT_SHELF_CLEAR ? g_fg : g_grey, a));
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    for (int s = 0; s < ShelfSlots(); s++) {
        D2D1_RECT_F r = ShelfTileRect(s);
        D2D1_RECT_F ir = D2D1::RectF(r.left + (kTileW - kIconSz) / 2, r.top + 2, r.left + (kTileW + kIconSz) / 2, r.top + 2 + kIconSz);
        if (ShelfOverflowSlot(s)) {
            g_brush->SetColor(WithAlpha(g_chip, a));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(ir, 10, 10), g_brush);
            wchar_t more[16];
            swprintf(more, 16, L"+%d", (int)g_shelf.size() - 4);
            Text(more, g_fmtDrop, ir, WithAlpha(g_fg, a));
            Text(L"more", g_fmtTile, D2D1::RectF(r.left, r.top + 44, r.right, r.top + 58), grey);
            continue;
        }
        const ShelfItem& it = g_shelf[s];
        if (it.selected) {
            g_brush->SetColor(D2D1::ColorF(0.25f, 0.52f, 1.0f, 0.35f * a));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - 2, r.top - 3, r.right + 2, r.bottom - 1), 10, 10), g_brush);
        }
        if (it.icon) {
            D2D1_SIZE_F bs = it.icon->GetSize(); // fit, keeping aspect ratio
            float k = std::min(kIconSz / bs.width, kIconSz / bs.height);
            float w = bs.width * k, h = bs.height * k;
            float cx = (ir.left + ir.right) / 2, cy = (ir.top + ir.bottom) / 2;
            g_rt->DrawBitmap(it.icon, D2D1::RectF(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2), a);
        } else {
            g_brush->SetColor(WithAlpha(g_chip, a));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(ir, 10, 10), g_brush);
            Text(L"\xE8A5", g_fmtIcon, ir, WithAlpha(g_fg, 0.7f * a)); // Document
        }
        Text(it.name.c_str(), g_fmtTile, D2D1::RectF(r.left - 2, r.top + 44, r.right + 2, r.top + 58), WithAlpha(g_fg, a));

        // remove button
        D2D1_POINT_2F c = ShelfCloseCenter(s);
        bool hot = g_hoverClose == s;
        g_brush->SetColor(WithAlpha(g_bg, a)); // ring in the pill colour, so it reads over any icon
        g_rt->FillEllipse(D2D1::Ellipse(c, kCloseR + 1.5f, kCloseR + 1.5f), g_brush);
        g_brush->SetColor(hot ? D2D1::ColorF(0.93f, 0.25f, 0.22f, a) : D2D1::ColorF(0.30f, 0.30f, 0.32f, a));
        g_rt->FillEllipse(D2D1::Ellipse(c, kCloseR, kCloseR), g_brush);
        g_brush->SetColor(WithAlpha(g_fg, a));
        float k = 3.0f;
        g_rt->DrawLine(D2D1::Point2F(c.x - k, c.y - k), D2D1::Point2F(c.x + k, c.y + k), g_brush, 1.6f);
        g_rt->DrawLine(D2D1::Point2F(c.x - k, c.y + k), D2D1::Point2F(c.x + k, c.y - k), g_brush, 1.6f);
    }
}

static void Text(const wchar_t* s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c) {
    g_brush->SetColor(c);
    g_rt->DrawText(s, (UINT32)wcslen(s), f, r, g_brush);
}

static float TextWidth(const wchar_t* s, IDWriteTextFormat* f) { // DIPs, one line
    IDWriteTextLayout* l = nullptr;
    float w = 0;
    if (SUCCEEDED(g_dw->CreateTextLayout(s, (UINT32)wcslen(s), f, 10000, 200, &l))) {
        DWRITE_TEXT_METRICS m;
        if (SUCCEEDED(l->GetMetrics(&m))) w = m.widthIncludingTrailingWhitespace;
        l->Release();
    }
    return w;
}

static const D2D1_COLOR_F kRed = {0.93f, 0.25f, 0.22f, 1};
static const D2D1_COLOR_F kGreen = {0.19f, 0.82f, 0.35f, 1};
// ---- Clipboard history: a Clipboard tab on the Shelf page ----
// While it's on, Perch listens for clipboard changes (AddClipboardFormatListener: no polling) and keeps
// the text you copy, newest first. Copies that ask to stay out of history (password managers mark them
// with ExcludeClipboardContentFromMonitorProcessing, CanIncludeInClipboardHistory = 0 or
// Clipboard Viewer Ignore) are skipped. With Remember on, the list is saved encrypted for this Windows
// account (DPAPI) in clipboard.dat; otherwise it lives only in memory.
static std::vector<std::wstring> g_clip;
                                    // g_shelfTab (above): 0 Files, 1 Clipboard
static int g_clipScroll = 0;        // first row shown
static int g_clipCopied = -1;       // row that just shows "Copied"
static DWORD g_clipOwnSeq = 0;      // clipboard sequence number of our own copy (not added again)
static bool g_clipListening = false;
static bool g_clipLoaded = false;   // clipboard.dat read (once the settings say whether to)
static std::wstring ClipFile() { return DataDir() + L"\\clipboard.dat"; }
static void SaveClip() {
    if (!g_set.clipRemember) { DeleteFileW(ClipFile().c_str()); return; }
    std::vector<BYTE> raw;
    for (const std::wstring& s : g_clip) {
        DWORD n = (DWORD)s.size();
        raw.insert(raw.end(), (BYTE*)&n, (BYTE*)&n + 4);
        raw.insert(raw.end(), (const BYTE*)s.data(), (const BYTE*)(s.data() + n));
    }
    DATA_BLOB in = {(DWORD)raw.size(), raw.data()}, out = {};
    if (!CryptProtectData(&in, L"Perch clipboard", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return;
    if (FILE* f = _wfopen(ClipFile().c_str(), L"wb")) { fwrite(out.pbData, 1, out.cbData, f); fclose(f); }
    LocalFree(out.pbData);
}
static void LoadClip() {
    g_clip.clear();
    if (!g_set.clipRemember) return;
    FILE* f = _wfopen(ClipFile().c_str(), L"rb");
    if (!f) return;
    std::vector<BYTE> enc;
    BYTE buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) enc.insert(enc.end(), buf, buf + n);
    fclose(f);
    DATA_BLOB in = {(DWORD)enc.size(), enc.data()}, out = {};
    if (enc.empty() || !CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return;
    for (DWORD p = 0; p + 4 <= out.cbData && (int)g_clip.size() < g_set.clipMax;) {
        DWORD len;
        memcpy(&len, out.pbData + p, 4);
        p += 4;
        if (p + len * 2 > out.cbData) break;
        g_clip.emplace_back((const wchar_t*)(out.pbData + p), len);
        p += len * 2;
    }
    LocalFree(out.pbData);
}
static bool ClipExcluded() { // the copy asked to stay out of clipboard history
    static UINT ex = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    static UINT can = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    static UINT ign = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    if (IsClipboardFormatAvailable(ex) || IsClipboardFormatAvailable(ign)) return true;
    if (IsClipboardFormatAvailable(can))
        if (HANDLE h = GetClipboardData(can))
            if (const DWORD* v = (const DWORD*)GlobalLock(h)) { bool no = *v == 0; GlobalUnlock(h); return no; }
    return false;
}
static void OnClipboardChanged() {
    if (!g_set.clipHistory || GetClipboardSequenceNumber() == g_clipOwnSeq) return;
    if (!OpenClipboard(g_hwnd)) return;
    std::wstring text;
    if (!ClipExcluded())
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { text.assign(p, wcsnlen(p, 20000)); GlobalUnlock(h); }
    CloseClipboard();
    if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos) return;
    g_clip.erase(std::remove(g_clip.begin(), g_clip.end(), text), g_clip.end()); // copied again: to the top
    g_clip.insert(g_clip.begin(), text);
    if ((int)g_clip.size() > g_set.clipMax) g_clip.resize(g_set.clipMax);
    g_clipCopied = -1;
    SaveClip();
}
static void ClipListen() { // follow the setting
    if (g_set.clipHistory && !g_clipLoaded) LoadClip(); // first time on (at start, once the settings are read)
    g_clipLoaded = true;
    if (g_set.clipHistory && !g_clipListening) g_clipListening = AddClipboardFormatListener(g_hwnd) != 0;
    else if (!g_set.clipHistory && g_clipListening) { RemoveClipboardFormatListener(g_hwnd); g_clipListening = false; }
    if (!g_set.clipHistory) { g_shelfTab = 0; g_clip.clear(); DeleteFileW(ClipFile().c_str()); }
    else {
        if ((int)g_clip.size() > g_set.clipMax) g_clip.resize(g_set.clipMax);
        SaveClip(); // Remember on: write it; off: the file goes
    }
}
static void ClipCopy(int i) { // put an item back on the clipboard
    if (i < 0 || i >= (int)g_clip.size() || !OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    const std::wstring& s = g_clip[i];
    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t))) {
        memcpy(GlobalLock(m), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(m);
        if (!SetClipboardData(CF_UNICODETEXT, m)) GlobalFree(m);
    }
    CloseClipboard();
    g_clipOwnSeq = GetClipboardSequenceNumber();
    g_clipCopied = i;
    SetTimer(g_hwnd, TIMER_CLIP, 1400, nullptr); // "Copied" for a moment
}
static void ClipRemove(int i) {
    if (i < 0 || i >= (int)g_clip.size()) return;
    g_clip.erase(g_clip.begin() + i);
    g_clipScroll = std::max(0, std::min(g_clipScroll, (int)g_clip.size() - kClipRows));
    g_clipCopied = -1;
    SaveClip();
}
static void ClipClear() { g_clip.clear(); g_clipScroll = 0; g_clipCopied = -1; SaveClip(); }

static bool ClipActive() { return g_set.clipHistory && g_shelfTab == 1; }

static void DrawShelfTabs(float a) {
    float cx = kWinW / 2;
    D2D1_RECT_F pill = D2D1::RectF(cx - 68, 46, cx + 70, 66);
    g_brush->SetColor(WithAlpha(g_bg, a)); // covers the zone's edge under it
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 10, 10), g_brush);
    g_brush->SetColor(WithAlpha(g_fg, 0.07f * a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 10, 10), g_brush);
    for (int t = 0; t < 2; t++) {
        D2D1_RECT_F r = ShelfTabRect(t);
        bool sel = g_shelfTab == t;
        if (sel || g_hot == HOT_SHELF_TAB0 + t) {
            g_brush->SetColor(WithAlpha(g_fg, (sel ? 0.18f : 0.09f) * a));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), g_brush);
        }
        Text(t ? L"Clipboard" : L"Files", g_fmtTile, r, WithAlpha(sel ? g_fg : g_grey, a));
    }
    if (g_shelfTab == 1 && !g_clip.empty())
        Text(L"Clear", g_fmtTile, ClipClearRect(), WithAlpha(g_hot == HOT_CLIP_CLEAR ? g_fg : g_grey, a));
}
static void DrawClipList(float a) {
    if (g_clip.empty()) {
        D2D1_RECT_F zone = D2D1::RectF(PX() + 32, 58, PX() + kExpW - 32, 147);
        g_brush->SetColor(WithAlpha(g_fg, 0.28f * a));
        g_rt->DrawRoundedRectangle(D2D1::RoundedRect(zone, 14, 14), g_brush, 1.2f, g_dash);
        Text(L"Text you copy shows up here", g_fmtDrop, D2D1::RectF(zone.left, zone.top + 28, zone.right, zone.top + 50), WithAlpha(g_fg, a));
        Text(L"Click an item to copy it again", g_fmtTile, D2D1::RectF(zone.left, zone.top + 50, zone.right, zone.top + 66), WithAlpha(g_grey, a));
        return;
    }
    for (int row = 0; row < kClipRows; row++) {
        int i = g_clipScroll + row;
        if (i >= (int)g_clip.size()) break;
        D2D1_RECT_F r = ClipRowRect(row);
        g_brush->SetColor(WithAlpha(g_fg, (g_hot == HOT_CLIP_ROW0 + row ? 0.12f : 0.06f) * a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), g_brush);
        std::wstring line = g_clip[i].substr(0, 120);
        for (wchar_t& c : line) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
        D2D1_RECT_F tr = D2D1::RectF(r.left + 10, r.top + 4, r.right - 30, r.bottom - 4);
        g_rt->PushAxisAlignedClip(tr, D2D1_ANTIALIAS_MODE_ALIASED);
        Text(line.c_str(), g_fmtMeta, tr, WithAlpha(g_fg, a));
        g_rt->PopAxisAlignedClip();
        if (g_clipCopied == i) {
            g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(L"Copied", g_fmtMeta, D2D1::RectF(r.right - 90, r.top + 4, r.right - 10, r.bottom - 4), WithAlpha(g_grey, a));
            g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        } else {
            D2D1_POINT_2F c = ClipDelCenter(row);
            bool hot = g_hot == HOT_CLIP_DEL0 + row;
            if (hot) { g_brush->SetColor(WithAlpha(kRed, 0.9f * a)); g_rt->FillEllipse(D2D1::Ellipse(c, 8, 8), g_brush); }
            g_brush->SetColor(WithAlpha(hot ? g_fg : g_grey, a));
            g_rt->DrawLine(D2D1::Point2F(c.x - 3, c.y - 3), D2D1::Point2F(c.x + 3, c.y + 3), g_brush, 1.4f, g_round);
            g_rt->DrawLine(D2D1::Point2F(c.x + 3, c.y - 3), D2D1::Point2F(c.x - 3, c.y + 3), g_brush, 1.4f, g_round);
        }
    }
}


// ---- 1.7 Glass: what's behind the notch, blurred, drawn inside its outline ----
// Windows' own blur only comes in rectangles, so its corners poked out past the notch's rounded ones.
// Instead Perch takes a small copy of the screen under its window (leaving itself out of the copy),
// blurs it, and draws it clipped to the notch's exact shape under the tint. A fresh copy is taken when
// the notch starts to open, every couple of seconds while it's open, and once when it has closed;
// nothing runs while it sits closed.
static void BoxBlur(DWORD* p, int w, int h, int r) { // one horizontal and one vertical pass
    std::vector<DWORD> tmp((size_t)w * h);
    for (int pass = 0; pass < 2; pass++) {
        const DWORD* src = pass ? tmp.data() : p;
        DWORD* dst = pass ? p : tmp.data();
        int n = pass ? h : w, lines = pass ? w : h;
        for (int l = 0; l < lines; l++)
            for (int i = 0; i < n; i++) {
                unsigned sr = 0, sg = 0, sb = 0, cnt = 0;
                for (int k = std::max(0, i - r); k <= std::min(n - 1, i + r); k++) {
                    DWORD c = pass ? src[(size_t)k * w + l] : src[(size_t)l * w + k];
                    sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; cnt++;
                }
                dst[pass ? (size_t)i * w + l : (size_t)l * w + i] = 0xFF000000u | ((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt);
            }
    }
}
static void GlassCapture() {
    if (!g_glassOn || !g_rt || g_hidden) return;
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    int W = wr.right - wr.left, H = wr.bottom - wr.top, sw = std::max(1, W / 4), sh = std::max(1, H / 4);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = sw;
    bi.bmiHeader.biHeight = -sh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC scr = GetDC(nullptr), mem = CreateCompatibleDC(scr);
    HBITMAP bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        SetWindowDisplayAffinity(g_hwnd, WDA_EXCLUDEFROMCAPTURE); // the copy shows what's behind, not Perch itself
        DwmFlush();
        SetStretchBltMode(mem, HALFTONE);
        StretchBlt(mem, 0, 0, sw, sh, scr, wr.left, wr.top, W, H, SRCCOPY | CAPTUREBLT);
        SetWindowDisplayAffinity(g_hwnd, WDA_NONE);
        GdiFlush();
        BoxBlur((DWORD*)bits, sw, sh, 3);
        BoxBlur((DWORD*)bits, sw, sh, 3); // twice: close to a soft Gaussian
        SafeRelease(g_glassBmp);
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(g_rt->CreateBitmap(D2D1::SizeU(sw, sh), bits, sw * 4, bp, &g_glassBmp))) g_glassAt = wr;
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, scr);
}
static void SetGlass(bool on) {
    g_glassOn = on;
    SafeRelease(g_glassBmp);
    if (on) GlassCapture();
}
// Under the tint: the blurred copy, inside the notch's outline.
static void DrawGlassBehind(ID2D1Geometry* shape) {
    if (!g_glassOn || !g_glassBmp || !shape) return;
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    if (!EqualRect(&wr, &g_glassAt)) return; // the window moved: wait for a fresh copy
    g_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), nullptr);
    g_rt->DrawBitmap(g_glassBmp, D2D1::RectF(0, -g_offY, kWinW, kWinH - g_offY), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    g_rt->PopLayer();
}

// ---- pages (open notch) ----
static float OpenHeight() { return g_toast ? (g_act ? kActH : kToastH) : kExpH + kIconBarH * g_iconBar; }
static float OpenWidth() { return g_toast && g_act ? kActW : kExpW; }

static void FormatClock(INT64 t100ns, wchar_t* out, size_t n, bool minus) { // "03:07", "1:02:03"
    long long s = t100ns / 10000000;
    if (s >= 3600) swprintf(out, n, L"%ls%lld:%02lld:%02lld", minus ? L"-" : L"", s / 3600, (s / 60) % 60, s % 60);
    else swprintf(out, n, L"%ls%02lld:%02lld", minus ? L"-" : L"", s / 60, s % 60);
}

// Solid lightning bolt centred on c, about 2*h tall (the charging mark inside the ring).
static void FillBolt(D2D1_POINT_2F c, float h, D2D1_COLOR_F col) {
    static const float pts[][2] = {{0.18f, -1.0f}, {-0.62f, 0.14f}, {-0.06f, 0.14f}, {-0.22f, 1.0f}, {0.62f, -0.18f}, {0.06f, -0.18f}};
    ID2D1PathGeometry* path = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(g_d2d->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
        sink->BeginFigure(D2D1::Point2F(c.x + pts[0][0] * h, c.y + pts[0][1] * h), D2D1_FIGURE_BEGIN_FILLED);
        for (int i = 1; i < 6; i++) sink->AddLine(D2D1::Point2F(c.x + pts[i][0] * h, c.y + pts[i][1] * h));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        g_brush->SetColor(col);
        g_rt->FillGeometry(path, g_brush);
    }
    SafeRelease(sink);
    SafeRelease(path);
}

// Battery ring (the "status ring"): arc = charge, green; red when low on battery; white bolt while plugged in.
// Size: centre-line radius 7.2, stroke 3.2 (22 px across at 125%).
static const D2D1_COLOR_F kRingGreen = {0.13f, 1.0f, 0.33f, 1}; // #21FF54
static const float kRingR = 7.2f, kRingStroke = 3.2f;
static void DrawRingArc(D2D1_POINT_2F c, float f, D2D1_COLOR_F col, float a) { // track + arc from the top, clockwise
    const float rad = kRingR;
    g_brush->SetColor(WithAlpha(g_fg, 0.14f * a));
    g_rt->DrawEllipse(D2D1::Ellipse(c, rad, rad), g_brush, kRingStroke);
    g_brush->SetColor(WithAlpha(col, a));
    f = Clamp01(f);
    if (f >= 0.995f) {
        g_rt->DrawEllipse(D2D1::Ellipse(c, rad, rad), g_brush, kRingStroke);
    } else if (f > 0) {
        ID2D1PathGeometry* path = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(g_d2d->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
            float a1 = -3.14159265f / 2, a2 = a1 + f * 2 * 3.14159265f;
            sink->BeginFigure(D2D1::Point2F(c.x + rad * cosf(a1), c.y + rad * sinf(a1)), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(c.x + rad * cosf(a2), c.y + rad * sinf(a2)), D2D1::SizeF(rad, rad), 0,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE, f > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
            ID2D1StrokeStyle* round = nullptr;
            g_d2d->CreateStrokeStyle(sp, nullptr, 0, &round);
            g_rt->DrawGeometry(path, g_brush, kRingStroke, round);
            SafeRelease(round);
        }
        SafeRelease(sink);
        SafeRelease(path);
    }
}
static void DrawRing(D2D1_POINT_2F c, float a) {
    if (g_battPct < 0) return; // desktop: no battery, no ring
    bool charging = g_onAC == 1;
    DrawRingArc(c, g_battPct / 100.0f, (!charging && g_battPct <= 20) ? kRed : kRingGreen, a);
    if (charging) FillBolt(c, 4.0f, WithAlpha(g_fg, a)); // white, about 8 tall
}
static float TasksDoneFraction(); // To-do: tasks done today / tasks (for the Tasks ring)

// Header row shared by every page: [page title]  (ring) Wed 18 Nov 2026 00:24
// Five thin bars, tallest in the middle, centred on c.
static void DrawSoundBars(D2D1_POINT_2F c, const float* hgt, float pitch, float a) {
    g_brush->SetColor(WithAlpha(g_fg, a));
    for (int i = 0; i < 5; i++) {
        float x = c.x + (i - 2) * pitch, hh = hgt[i] / 2;
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 1, c.y - hh, x + 1, c.y + hh), 1, 1), g_brush);
    }
}
static void DrawHeaderStatus(float a) {
    float px = PX(), y = 27;
    DrawRing(D2D1::Point2F(px + 120, y), a);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t dateStr[64] = L"", timeStr[32] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd d MMM yyyy", dateStr, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, timeStr, 32);
    float dx = px + (g_battPct < 0 ? 110 : 138);
    Text(dateStr, g_fmtHdrDate, D2D1::RectF(dx, y - 10, px + kExpW - 20, y + 10), WithAlpha(g_fg, a));
    dx += TextWidth(dateStr, g_fmtHdrDate) + 6.4f;
    Text(timeStr, g_fmtHdrTime, D2D1::RectF(dx, y - 10, px + kExpW - 20, y + 10), WithAlpha(g_fg, a));
    if (g_media.hasSession && g_media.playing) {
        static const float hb[5] = {4, 6.4f, 9.6f, 10.4f, 4.8f};
        DrawSoundBars(D2D1::Point2F(px + 357.6f, y), hb, 4.4f, a);
    }
}

static void DrawHotCircle(int hot, float a) { // hover highlight behind a control
    if (g_hot != hot) return;
    D2D1_POINT_2F c = HotCenter(hot);
    float r = HotRadius(hot);
    g_brush->SetColor(WithAlpha(g_chip, a));
    g_rt->FillEllipse(D2D1::Ellipse(c, r, r), g_brush);
}

// Filled triangle centred on c, pointing right (dir +1), left (-1) or down (2) (media buttons).
static void FillTriangle(D2D1_POINT_2F c, float size, int dir) {
    ID2D1PathGeometry* path = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(g_d2d->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
        float hw = size * 0.5f, hh = size * 0.58f;
        D2D1_POINT_2F p[3];
        if (dir == 2) { p[0] = D2D1::Point2F(c.x - hh, c.y - hw); p[1] = D2D1::Point2F(c.x, c.y + hw); p[2] = D2D1::Point2F(c.x + hh, c.y - hw); }
        else { p[0] = D2D1::Point2F(c.x - hw * dir, c.y - hh); p[1] = D2D1::Point2F(c.x + hw * dir, c.y); p[2] = D2D1::Point2F(c.x - hw * dir, c.y + hh); }
        sink->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(p[1]);
        sink->AddLine(p[2]);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        g_rt->FillGeometry(path, g_brush);
    }
    SafeRelease(sink);
    SafeRelease(path);
}

// The shelf icon: an open tray with an arrow dropping into it.
static void DrawTrayIcon(D2D1_POINT_2F c, float s, D2D1_COLOR_F col, D2D1_COLOR_F behind) {
    g_brush->SetColor(col);
    D2D1_RECT_F tray = D2D1::RectF(c.x - s * 0.55f, c.y + s * 0.05f, c.x + s * 0.55f, c.y + s * 0.55f);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(tray, s * 0.12f, s * 0.12f), g_brush);
    behind.a = col.a; // the tray's opening shows what's behind
    g_brush->SetColor(behind);
    g_rt->FillRectangle(D2D1::RectF(c.x - s * 0.22f, c.y + s * 0.05f, c.x + s * 0.22f, c.y + s * 0.22f), g_brush);
    g_brush->SetColor(col);
    g_rt->FillRectangle(D2D1::RectF(c.x - s * 0.07f, c.y - s * 0.6f, c.x + s * 0.07f, c.y - s * 0.02f), g_brush);
    FillTriangle(D2D1::Point2F(c.x, c.y + s * 0.02f), s * 0.42f, 2);
}

static void DrawMusicPage(float a) {
    float px = PX();
    D2D1_COLOR_F fg = WithAlpha(g_fg, a), grey = WithAlpha(g_grey, a);
    DrawArt(D2D1::RectF(px + 32, 24, px + 32 + kArtSize, 24 + kArtSize), 12, a);
    bool has = g_media.hasSession;
    Text(has ? (g_media.title[0] ? g_media.title : L"Unknown title") : L"Not Playing", g_fmtTitle, D2D1::RectF(px + 110, 46, px + kExpW - 32, 66), fg);
    if (has) Text(g_media.artist, g_fmtArtist, D2D1::RectF(px + 110, 66, px + kExpW - 32, 82), grey);

    // progress: elapsed, bar, remaining
    INT64 len = has ? g_media.length : 0, pos = has ? Media_Position(g_media) : 0;
    wchar_t left[24], right[24];
    FormatClock(pos, left, 24, false);
    FormatClock(len > pos ? len - pos : 0, right, 24, true);
    Text(left, g_fmtMeta, D2D1::RectF(px + 32, 99, px + 82, 115), grey);
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    Text(right, g_fmtMeta, D2D1::RectF(px + 300, 99, px + 368, 115), grey);
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    D2D1_RECT_F track = D2D1::RectF(px + 84, 105.5f, px + 306, 108.5f);
    g_brush->SetColor(WithAlpha(g_fg, 0.16f * a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(track, 1.5f, 1.5f), g_brush);
    if (len > 0 && pos > 0) {
        D2D1_RECT_F fill = track;
        fill.right = track.left + (track.right - track.left) * Clamp01((float)pos / len);
        g_brush->SetColor(WithAlpha(g_fg, 0.85f * a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 1.5f, 1.5f), g_brush);
    }

    // controls: lyrics, previous, play/pause, next, mute
    for (int hot : {HOT_PREV, HOT_PLAY, HOT_NEXT, HOT_VOLUME}) DrawHotCircle(hot, a);
    auto glyph = [&](int hot, const wchar_t* g, IDWriteTextFormat* f, float alpha) {
        D2D1_POINT_2F c = HotCenter(hot);
        Text(g, f, D2D1::RectF(c.x - 16, c.y - 16, c.x + 16, c.y + 16), WithAlpha(g_fg, alpha * a));
    };
    float on = has ? 0.92f : 0.45f;
    glyph(HOT_LYRICS, L"\xE8BD", g_fmtCtl, 0.45f); // lyrics (coming soon)
    // previous / next: two filled triangles; play: one; pause: two bars
    g_brush->SetColor(WithAlpha(g_fg, on * a));
    D2D1_POINT_2F pv = HotCenter(HOT_PREV), nx = HotCenter(HOT_NEXT), pl = HotCenter(HOT_PLAY);
    FillTriangle(D2D1::Point2F(pv.x - 4, pv.y), 9, -1);
    FillTriangle(D2D1::Point2F(pv.x + 4, pv.y), 9, -1);
    FillTriangle(D2D1::Point2F(nx.x - 4, nx.y), 9, 1);
    FillTriangle(D2D1::Point2F(nx.x + 4, nx.y), 9, 1);
    g_brush->SetColor(WithAlpha(g_fg, (has ? 1.0f : 0.45f) * a));
    if (g_media.playing) {
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(pl.x - 6, pl.y - 7.5f, pl.x - 2, pl.y + 7.5f), 1, 1), g_brush);
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(pl.x + 2, pl.y - 7.5f, pl.x + 6, pl.y + 7.5f), 1, 1), g_brush);
    } else {
        FillTriangle(D2D1::Point2F(pl.x + 1.5f, pl.y), 13, 1);
    }
    BOOL muted = FALSE;
    if (g_outVol) g_outVol->GetMute(&muted);
    glyph(HOT_VOLUME, muted ? L"\xE74F" : L"\xE767", g_fmtCtl, 0.92f); // Mute / Volume
}

// ---- Pomodoro page ----
// A round is 4 focus sessions with a short break after each of the first three, then a long break.
// The timer is one SetTimer for the end of the current phase; the page redraws once a second only
// while the notch is open (TIMER_CLOCK). Breaks start by themselves after a focus session, the next
// focus after a short break; after the long break the round goes back to Ready.
// Finished focus minutes go to focus.txt ("YYYYMMDD sessions minutes" per day) for Focus stats.
enum PomoPhase { POMO_FOCUS = 0, POMO_SHORT, POMO_LONG };
enum PomoState { POMO_READY = 0, POMO_RUNNING, POMO_PAUSED };
static int g_pomoState = POMO_READY, g_pomoPhase = POMO_FOCUS, g_pomoSession = 0; // session 0..3
static ULONGLONG g_pomoEnd;   // running: tick count when the phase ends
static INT64 g_pomoLeft;      // paused: ms left
static int g_chimes;          // tests: how many chimes played

static int PomoPhaseMin(int ph) { return ph == POMO_FOCUS ? g_set.pomoFocus : ph == POMO_SHORT ? g_set.pomoShort : g_set.pomoLong; }
static INT64 PomoLeftMs() {
    if (g_pomoState == POMO_RUNNING) { ULONGLONG now = GetTickCount64(); return now >= g_pomoEnd ? 0 : (INT64)(g_pomoEnd - now); }
    if (g_pomoState == POMO_PAUSED) return g_pomoLeft;
    return (INT64)PomoPhaseMin(g_pomoPhase) * 60000;
}
static INT64 PomoRoundLeftMs() { // this phase plus everything after it in the round
    INT64 ms = PomoLeftMs();
    for (int s = g_pomoSession, ph = g_pomoPhase;;) {
        if (ph == POMO_FOCUS) ph = s < 3 ? POMO_SHORT : POMO_LONG;
        else if (ph == POMO_SHORT) { s++; ph = POMO_FOCUS; }
        else break;
        ms += (INT64)PomoPhaseMin(ph) * 60000;
    }
    return ms;
}
static void FormatDuration(INT64 ms, wchar_t* out, size_t n) { // "2 h 10 min", "45 min", "1 h"
    long long m = (ms + 59999) / 60000, h = m / 60;
    m %= 60;
    if (h && m) swprintf(out, n, L"%lld h %lld min", h, m);
    else if (h) swprintf(out, n, L"%lld h", h);
    else swprintf(out, n, L"%lld min", m);
}

static void Chime() {
    if (!g_set.sounds) return;
    g_chimes++;
    if (!PlaySoundW(L"Notification.Reminder", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) MessageBeep(MB_OK);
}

static std::wstring FocusFile() { return DataDir() + L"\\focus.txt"; }
static int TodayStamp() { SYSTEMTIME st; GetLocalTime(&st); return st.wYear * 10000 + st.wMonth * 100 + st.wDay; }
static int FocusMinutesToday() {
    int day, sessions, minutes, total = 0, today = TodayStamp();
    FILE* f = _wfopen(FocusFile().c_str(), L"r");
    if (!f) return 0;
    while (fscanf(f, "%d %d %d", &day, &sessions, &minutes) == 3) if (day == today) total = minutes;
    fclose(f);
    return total;
}
static void RecordFocus(int minutes) { // adds one session to today's line, keeps the other days
    std::vector<int> days, sess, mins;
    int day, a, b, today = TodayStamp();
    if (FILE* f = _wfopen(FocusFile().c_str(), L"r")) {
        while (fscanf(f, "%d %d %d", &day, &a, &b) == 3) { days.push_back(day); sess.push_back(a); mins.push_back(b); }
        fclose(f);
    }
    if (days.empty() || days.back() != today) { days.push_back(today); sess.push_back(0); mins.push_back(0); }
    sess.back()++;
    mins.back() += minutes;
    if (FILE* f = _wfopen(FocusFile().c_str(), L"w")) {
        size_t from = days.size() > 400 ? days.size() - 400 : 0; // about a year is plenty
        for (size_t i = from; i < days.size(); i++) fprintf(f, "%d %d %d\n", days[i], sess[i], mins[i]);
        fclose(f);
    }
}

static void PomoArm() { // one timer for the end of the running phase
    if (g_pomoState == POMO_RUNNING) SetTimer(g_hwnd, TIMER_POMO, (UINT)PomoLeftMs() + 20, nullptr);
    else KillTimer(g_hwnd, TIMER_POMO);
    PetRecheck(); // the mascot steps aside while a Pomodoro runs
}
static void PomoStartPhase(int phase) {
    g_pomoPhase = phase;
    g_pomoState = POMO_RUNNING;
    g_pomoEnd = GetTickCount64() + (ULONGLONG)PomoPhaseMin(phase) * 60000;
    PomoArm();
}
static void PomoReset() {
    g_pomoState = POMO_READY;
    g_pomoPhase = POMO_FOCUS;
    g_pomoSession = 0;
    PomoArm();
}
static void PomoToggle() { // Start / Pause / Resume
    if (g_pomoState == POMO_READY) PomoStartPhase(g_pomoPhase);
    else if (g_pomoState == POMO_RUNNING) { g_pomoLeft = PomoLeftMs(); g_pomoState = POMO_PAUSED; PomoArm(); }
    else { g_pomoEnd = GetTickCount64() + (ULONGLONG)g_pomoLeft; g_pomoState = POMO_RUNNING; PomoArm(); }
}
static void PomoPhaseEnded() { // a pop-up for each part of the round
    wchar_t sub[64];
    Chime();
    if (g_pomoPhase == POMO_FOCUS) {
        RecordFocus(g_set.pomoFocus);
        if (g_pomoSession < 3) {
            swprintf(sub, 64, L"Take a %d min break", g_set.pomoShort);
            PomoStartPhase(POMO_SHORT);
            ShowToast(L"Focus finished", sub, L"\xE916");
        } else {
            swprintf(sub, 64, L"Enjoy a %d min break", g_set.pomoLong);
            PomoStartPhase(POMO_LONG);
            ShowToast(L"Round finished", sub, L"\xE916");
        }
    } else if (g_pomoPhase == POMO_SHORT) {
        g_pomoSession++;
        swprintf(sub, 64, L"Focus session %d of 4", g_pomoSession + 1);
        PomoStartPhase(POMO_FOCUS);
        ShowToast(L"Break finished", sub, L"\xE916");
    } else {
        PomoReset();
        ShowToast(L"Break finished", L"Start a new round whenever you like", L"\xE916");
    }
}

static ID2D1LinearGradientBrush* PurpleBrush(float x0, float x1, float a) { // #616AFE -> #A057F8
    D2D1_GRADIENT_STOP st[2] = {{0, D2D1::ColorF(0.38f, 0.42f, 1.0f, a)}, {1, D2D1::ColorF(0.63f, 0.34f, 0.97f, a)}};
    ID2D1GradientStopCollection* stops = nullptr;
    ID2D1LinearGradientBrush* b = nullptr;
    if (SUCCEEDED(g_rt->CreateGradientStopCollection(st, 2, &stops))) {
        g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x0, 0), D2D1::Point2F(x1, 0)), stops, &b);
        stops->Release();
    }
    return b;
}
static const D2D1_COLOR_F kPurple = {0.66f, 0.38f, 0.98f, 1}; // "Ready" / "Paused"

// Layout: left column timer, a divider at 181, the round on the right.
static void DrawPomodoroPage(float a) {
    float px = PX();
    D2D1_COLOR_F fg = WithAlpha(g_fg, a), grey = WithAlpha(g_grey, a);
    Text(L"Pomodoro", g_fmtHdr, D2D1::RectF(px + 32, 21.6f, px + 120, 41.6f), fg);
    wchar_t buf[64];
    INT64 left = PomoLeftMs(), len = (INT64)PomoPhaseMin(g_pomoPhase) * 60000;

    // left: phase, time left, minutes + state, progress
    if (g_pomoPhase == POMO_FOCUS) swprintf(buf, 64, L"Focus \x00B7 %d of 4", g_pomoSession + 1);
    else wcscpy(buf, g_pomoPhase == POMO_SHORT ? L"Short break" : L"Long break");
    Text(buf, g_fmtPomoSmall, D2D1::RectF(px + 34, 50, px + 178, 64.4f), grey);
    long long secs = (left + 999) / 1000;
    swprintf(buf, 64, L"%02lld:%02lld", secs / 60, secs % 60);
    Text(buf, g_fmtPomoTime, D2D1::RectF(px + 33, 66, px + 180, 102), fg);
    if (g_pomoState == POMO_READY) swprintf(buf, 64, L"%d min", PomoPhaseMin(g_pomoPhase));
    else swprintf(buf, 64, L"%lld min left", (left + 59999) / 60000);
    D2D1_RECT_F line = D2D1::RectF(px + 34, 108, px + 180, 127);
    Text(buf, g_fmtPomoSub, line, fg);
    if (g_pomoState != POMO_RUNNING) {
        line.left += TextWidth(buf, g_fmtPomoSub) + 6;
        line.top += 1.2f; line.bottom += 1.2f; // same baseline as the bigger text
        Text(g_pomoState == POMO_READY ? L"Ready" : L"Paused", g_fmtPomoState, line, WithAlpha(kPurple, a));
    }
    D2D1_RECT_F track = D2D1::RectF(px + 34, 133, px + 162, 137);
    g_brush->SetColor(WithAlpha(g_fg, 0.14f * a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), g_brush);
    float done = len > 0 ? Clamp01(1 - (float)left / len) : 0;
    if (done > 0) {
        D2D1_RECT_F fill = track;
        fill.right = track.left + (track.right - track.left) * done;
        if (ID2D1LinearGradientBrush* b = PurpleBrush(track.left, track.right, a)) { g_rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 2, 2), b); b->Release(); }
    }

    g_brush->SetColor(WithAlpha(g_fg, 0.1f * a));
    g_rt->FillRectangle(D2D1::RectF(px + 180.5f, 54, px + 181.5f, 140), g_brush);

    // right: this round, Start / Pause / Resume, reset, the round's segments
    Text(L"This round", g_fmtPomoSmall, D2D1::RectF(px + 194, 53.2f, px + 278, 69.2f), grey);
    D2D1_RECT_F btn = D2D1::RectF(px + 280, 53.6f, px + 340, 74.4f);
    if (ID2D1LinearGradientBrush* b = PurpleBrush(btn.left, btn.right, a * (g_hot == HOT_POMO_START ? 1.0f : 0.92f))) {
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(btn, 10.4f, 10.4f), b);
        b->Release();
    }
    Text(g_pomoState == POMO_READY ? L"Start" : g_pomoState == POMO_RUNNING ? L"Pause" : L"Resume", g_fmtPomoBtn, btn, fg);
    D2D1_POINT_2F rc = HotCenter(HOT_POMO_RESET);
    g_brush->SetColor(WithAlpha(g_hot == HOT_POMO_RESET ? g_fg : g_chip, (g_hot == HOT_POMO_RESET ? 0.22f : 1.0f) * a));
    g_rt->FillEllipse(D2D1::Ellipse(rc, 9, 9), g_brush);
    D2D1_MATRIX_3X2_F page;
    g_rt->GetTransform(&page); // the page may be sliding
    g_rt->SetTransform(D2D1::Matrix3x2F::Scale(-1, 1, rc) * page);
    Text(L"\xE72C", g_fmtChevron, D2D1::RectF(rc.x - 9, rc.y - 9, rc.x + 9, rc.y + 9), WithAlpha(g_fg, 0.85f * a)); // Refresh, mirrored: anticlockwise
    g_rt->SetTransform(page);

    static const float seg[8][2] = {{194.4f, 222.4f}, {226.4f, 231.2f}, {234.4f, 262.4f}, {266.4f, 271.2f},
                                    {275.2f, 303.2f}, {307.2f, 312.0f}, {315.2f, 343.2f}, {347.2f, 364.0f}};
    int cur = g_pomoPhase == POMO_FOCUS ? g_pomoSession * 2 : g_pomoPhase == POMO_SHORT ? g_pomoSession * 2 + 1 : 7;
    for (int i = 0; i < 8; i++) {
        D2D1_RECT_F r = D2D1::RectF(px + seg[i][0], 95.2f, px + seg[i][1], 100.8f);
        g_brush->SetColor(WithAlpha(g_fg, 0.12f * a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 2.8f, 2.8f), g_brush);
        float f = i < cur ? 1 : i == cur && g_pomoState != POMO_READY ? done : 0;
        if (f > 0) {
            D2D1_RECT_F fr = r;
            fr.right = r.left + (r.right - r.left) * f;
            if (ID2D1LinearGradientBrush* b = PurpleBrush(r.left, r.right, a)) { g_rt->FillRoundedRectangle(D2D1::RoundedRect(fr, 2.8f, 2.8f), b); b->Release(); }
        }
    }
    static const wchar_t* labels[5] = {L"1", L"2", L"3", L"4", L"Long"};
    static const int labelSeg[5] = {0, 2, 4, 6, 7};
    int curLabel = g_pomoPhase == POMO_LONG ? 4 : g_pomoSession;
    for (int i = 0; i < 5; i++) {
        float cx = px + (seg[labelSeg[i]][0] + seg[labelSeg[i]][1]) / 2;
        Text(labels[i], g_fmtPomoLabel, D2D1::RectF(cx - 20, 109, cx + 20, 123), i == curLabel ? fg : grey);
    }
    if (g_pomoState == POMO_READY) {
        wchar_t d[32];
        FormatDuration(PomoRoundLeftMs(), d, 32);
        swprintf(buf, 64, L"%ls \x00B7 4 sessions", d);
    } else {
        FormatDuration(PomoRoundLeftMs(), buf, 48);
        wcscat(buf, L" until done");
    }
    Text(buf, g_fmtPomoFoot, D2D1::RectF(px + 194, 124.5f, px + 370, 139.5f), grey);
}

// ---- To-do page and task reminders ----
// Tasks live in tasks.txt, one per line, tab-separated:
//   doneDay  daily  startShown  start  end  endShown  title
// (days as YYYYMMDD, 0 = not; start/end as minutes since midnight, -1 = no time). A daily task counts
// as done only on the day it was ticked; a one-off task that was done on an earlier day is dropped.
// Reminders ("Task Time!" when a task starts, "Time's Up!" when its time is up) use one SetTimer for
// the next due minute, re-armed after each one, and on clock changes.
struct Task {
    std::wstring title;
    int start = -1, end = -1;
    bool daily = false;
    int doneDay = 0, startShown = 0, endShown = 0;
    int remindAt = -1; // "Later": remind again at this minute (not saved)
};
static std::vector<Task> g_tasks;
static int g_tasksDay;                 // day the list was last rolled over
static int g_todoEditMode = 0;         // Edit Task: rows show up / down / delete
static int g_todoScroll = 0;           // first row shown
static int g_edit = 0;                 // editor: 0 closed, 1 new task, 2 editing g_editIdx
static int g_editIdx = -1, g_editField = 0; // 0 title, 1 time
static std::wstring g_editTitle, g_editTime; // time: up to 8 digits, HHMMHHMM
static bool g_editDaily = false, g_editBadTitle = false, g_editBadTime = false;
struct Reminder { int task; int kind; }; // kind 1 = Task Time!, 2 = Time's Up!, 3 = water (task -1), 4 = calendar event (task: g_calRemList)
static std::vector<Reminder> g_remQueue;
static Reminder g_rem = {-1, 0};          // the one showing

// Calendar: the To-do page's calendar button turns the page into a calendar.
// Left: the selected day (date, weekday) and what's on it; right: the month (or week).
// Events come from the Google Calendar iCal link in settings.ini, downloaded every 30 minutes.
enum CalStatus { CAL_NONE = 0, CAL_CHECKING, CAL_CONNECTED, CAL_UNREACHABLE, CAL_NOT_ICAL };
static bool g_todoCal = false;          // the calendar is showing
static bool g_calWeekView = false;      // Week instead of Month
static int g_calSel = 0, g_calAnchor = 0; // selected day; a day in the month (week) shown
static int g_calScroll = 0;             // first agenda line shown
static std::vector<CalEvent> g_calEvents;
static int g_calStatus = CAL_NONE;
static bool g_calBusy = false, g_calAnnounce = false, g_calFirst = true;
static std::wstring g_calLinkInUse;     // link g_calEvents came from
static wchar_t g_calUpdatedAt[16];      // time of the last good download, "14:02"
struct CalJob { std::wstring link; std::string body; int code = 1; };

static const D2D1_COLOR_F kTodoGreen = {0.19f, 0.78f, 0.35f, 1};    // #30C759: "List", ticks, active buttons
static const D2D1_COLOR_F kTodoGreenBg = {0.07f, 0.17f, 0.09f, 1};  // #122B17: done rows, active buttons
static const D2D1_COLOR_F kSaveGreen = {0.25f, 0.72f, 0.33f, 1};    // #40B854
static D2D1_COLOR_F Rgb(int v, float a = 1) { return D2D1::ColorF(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, a); }

static int g_testMinute = -1; // tests: pretend it is this minute of the day (WM_APP_TODO wp 1)
static int NowMinute() { if (g_testMinute >= 0) return g_testMinute; SYSTEMTIME st; GetLocalTime(&st); return st.wHour * 60 + st.wMinute; }
static bool TaskDone(const Task& t, int today) { return t.daily ? t.doneDay == today : t.doneDay != 0; }
static float TasksDoneFraction() {
    if (g_tasks.empty()) return 0;
    int done = 0, today = TodayStamp();
    for (const Task& t : g_tasks) done += TaskDone(t, today);
    return (float)done / g_tasks.size();
}
static std::wstring TasksFile() { return DataDir() + L"\\tasks.txt"; }
static void SaveTasks() {
    FILE* f = _wfopen(TasksFile().c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    for (const Task& t : g_tasks) {
        std::wstring title = t.title;
        for (wchar_t& c : title) if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
        fwprintf(f, L"%d\t%d\t%d\t%d\t%d\t%d\t%ls\n", t.doneDay, t.daily ? 1 : 0, t.startShown, t.start, t.end, t.endShown, title.c_str());
    }
    fclose(f);
}
static void RollTasks(int today) { // a new day: one-off tasks done earlier go
    if (g_tasksDay == today) return;
    g_tasksDay = today;
    size_t n = g_tasks.size();
    g_tasks.erase(std::remove_if(g_tasks.begin(), g_tasks.end(), [&](const Task& t) { return !t.daily && t.doneDay && t.doneDay < today; }), g_tasks.end());
    for (Task& t : g_tasks) t.remindAt = -1;
    if (g_tasks.size() != n) SaveTasks();
}
static void LoadTasks() {
    g_tasks.clear();
    g_tasksDay = 0;
    FILE* f = _wfopen(TasksFile().c_str(), L"r, ccs=UTF-8");
    if (f) {
        wchar_t line[512];
        while (fgetws(line, 512, f)) {
            Task t;
            wchar_t* p = line;
            int v[6];
            bool ok = true;
            for (int i = 0; i < 6 && ok; i++) {
                wchar_t* e = nullptr;
                v[i] = (int)wcstol(p, &e, 10);
                ok = e != p && *e == L'\t';
                p = e + 1;
            }
            if (!ok) continue;
            t.doneDay = v[0]; t.daily = v[1] != 0; t.startShown = v[2]; t.start = v[3]; t.end = v[4]; t.endShown = v[5];
            t.title = p;
            while (!t.title.empty() && (t.title.back() == L'\n' || t.title.back() == L'\r')) t.title.pop_back();
            if (!t.title.empty()) g_tasks.push_back(t);
        }
        fclose(f);
    }
    RollTasks(TodayStamp());
}

// ---- Water ----
// Today's water in ml. water.txt keeps one line per day, "YYYYMMDD ml". Reminders split the goal into
// cups spread evenly over the active hours: at each cup's time, if you're behind, a pop-up asks
// (Later / Drank). One SetTimer for the next such time; none once the goal is reached or reminders are off.
static int g_waterDay = 0, g_waterMl = 0;
static int g_waterLater = -1; // "Later": remind again at this minute (today)
static const D2D1_COLOR_F kWaterBlue = {0.30f, 0.68f, 1.0f, 1}; // #4CADFF
static std::wstring WaterFile() { return DataDir() + L"\\water.txt"; }
static void LoadWater() {
    int today = TodayStamp();
    g_waterDay = today;
    g_waterMl = 0;
    g_waterLater = -1;
    if (FILE* f = _wfopen(WaterFile().c_str(), L"r")) {
        int d, ml;
        while (fwscanf(f, L"%d %d", &d, &ml) == 2) if (d == today) g_waterMl = std::max(0, ml);
        fclose(f);
    }
}
static void SaveWater() {
    std::vector<std::pair<int, int>> days;
    if (FILE* f = _wfopen(WaterFile().c_str(), L"r")) {
        int d, ml;
        while (fwscanf(f, L"%d %d", &d, &ml) == 2) if (d != g_waterDay) days.push_back({d, ml});
        fclose(f);
    }
    days.push_back({g_waterDay, g_waterMl});
    if (days.size() > 400) days.erase(days.begin(), days.end() - 400); // about a year
    if (FILE* f = _wfopen(WaterFile().c_str(), L"w")) {
        for (auto& p : days) fwprintf(f, L"%d %d\n", p.first, p.second);
        fclose(f);
    }
}
static int WaterToday() { return g_waterDay == TodayStamp() ? g_waterMl : 0; } // a new day starts at 0
static float WaterFraction() { return g_set.waterGoal > 0 ? Clamp01((float)WaterToday() / g_set.waterGoal) : 0; }
static int WaterCups() { return std::max(1, (g_set.waterGoal + g_set.waterCup - 1) / g_set.waterCup); }
static int WaterNextDue(int now) { // the next minute a reminder is due, or -1
    if (!g_set.waterRemind || WaterToday() >= g_set.waterGoal) return -1;
    if (g_waterLater > now) return g_waterLater;
    int n = WaterCups(), span = g_set.waterUntil - g_set.waterFrom, ml = WaterToday();
    for (int k = 1; k <= n; k++) {
        int t = g_set.waterFrom + span * k / n;
        if (t > now && ml < std::min(k * g_set.waterCup, g_set.waterGoal)) return t;
    }
    return -1;
}
static void ArmWaterTimer() {
    int now = NowMinute(), due = WaterNextDue(now);
    if (due < 0) due = 24 * 60; // midnight: a new day
    SYSTEMTIME st;
    GetLocalTime(&st);
    long long ms = (long long)(due - now) * 60000 - st.wSecond * 1000 - st.wMilliseconds + 300;
    SetTimer(g_hwnd, TIMER_WATER, (UINT)std::max(1000LL, ms), nullptr);
}
static void ShowNextReminder();
static void OnWaterTimer() {
    int now = NowMinute();
    if (g_waterDay != TodayStamp()) LoadWater();
    bool due = g_waterLater >= 0 ? now >= g_waterLater : true;
    if (g_waterLater >= 0 && now >= g_waterLater) g_waterLater = -1;
    int ml = WaterToday(), n = WaterCups(), span = g_set.waterUntil - g_set.waterFrom, behind = 0;
    for (int k = 1; k <= n; k++) if (g_set.waterFrom + span * k / n <= now) behind = std::min(k * g_set.waterCup, g_set.waterGoal);
    bool inHours = now >= g_set.waterFrom && now <= g_set.waterUntil + 1;
    if (due && g_set.waterRemind && inHours && ml < behind && ml < g_set.waterGoal) {
        bool queued = (g_act && g_rem.kind == 3);
        for (const Reminder& r : g_remQueue) queued |= r.kind == 3;
        if (!queued) g_remQueue.push_back({-1, 3});
        if (!g_act) ShowNextReminder();
    }
    ArmWaterTimer();
}
static void WaterAdd(int ml) { // + a cup, or - to take one back
    if (g_waterDay != TodayStamp()) LoadWater();
    g_waterMl = std::max(0, std::min(g_waterMl + ml, 20000));
    SaveWater();
    ArmWaterTimer();
    if (SettingsUI_IsOpen()) SettingsUI_Refresh(g_set); // "Today" in the Water section
}
static void FormatLitres(int ml, wchar_t* out, int n) { // 1250 -> "1.25 L", 2000 -> "2 L", 300 -> "300 ml"
    if (ml < 1000) { swprintf(out, n, L"%d ml", ml); return; }
    wchar_t b[16];
    swprintf(b, 16, L"%.2f", ml / 1000.0);
    size_t len = wcslen(b);
    while (len && b[len - 1] == L'0') b[--len] = 0;
    if (len && b[len - 1] == L'.') b[--len] = 0;
    swprintf(out, n, L"%ls L", b);
}
static void UiWaterStatus(wchar_t* out, int n) {
    wchar_t a[16], g[16];
    FormatLitres(WaterToday(), a, 16);
    FormatLitres(g_set.waterGoal, g, 16);
    swprintf(out, n, L"%ls of %ls", a, g);
}

// ---- Calendar event reminders ----
// A pop-up a few minutes before each timed event of today ([Calendar] Remind: minutes before, 0 = at the
// start, -1 = off). One SetTimer for the next one; events already reminded are remembered for the day.
static std::vector<CalEvent> g_calRemList;  // events whose pop-up was queued (a Reminder's task is an index here)
static std::vector<std::wstring> g_calRemDone; // "day:start:title" of events already reminded
static std::vector<std::pair<CalEvent, int>> g_calLater; // "Later": event, minute to remind again
static const D2D1_COLOR_F kCalOrange = {1.0f, 0.62f, 0.04f, 1}; // #FF9F0A
static std::wstring CalRemKey(const CalEvent& e) { return std::to_wstring(e.day) + L":" + std::to_wstring(e.start) + L":" + e.title; }
static bool CalReminded(const CalEvent& e) { return std::find(g_calRemDone.begin(), g_calRemDone.end(), CalRemKey(e)) != g_calRemDone.end(); }
static int CalRemNextDue(int now) { // next minute a calendar pop-up is due, or -1
    int next = -1, today = Cal_Today();
    auto take = [&](int t) { if (t > now && (next < 0 || t < next)) next = t; };
    for (auto& l : g_calLater) take(l.second);
    if (g_set.calRemind < 0) return next;
    for (const CalEvent& e : g_calEvents)
        if (e.day == today && e.start >= 0 && !CalReminded(e)) take(std::max(0, e.start - g_set.calRemind));
    return next;
}
static void ArmCalRemTimer() {
    if (!g_hwnd) return;
    int now = NowMinute(), due = CalRemNextDue(now);
    if (due < 0) due = 24 * 60; // midnight: a new day's events
    SYSTEMTIME st;
    GetLocalTime(&st);
    long long ms = (long long)(due - now) * 60000 - st.wSecond * 1000 - st.wMilliseconds + 300;
    SetTimer(g_hwnd, TIMER_CALREM, (UINT)std::max(1000LL, ms), nullptr);
}
static void ShowNextReminder();
static void CheckCalReminders(int now) {
    int today = Cal_Today();
    if (!g_calRemDone.empty() && g_calRemDone.front().compare(0, std::to_wstring(today).size(), std::to_wstring(today)) != 0)
        g_calRemDone.clear(); // a new day
    bool queued = false;
    auto queue = [&](const CalEvent& e) { g_calRemList.push_back(e); g_remQueue.push_back({(int)g_calRemList.size() - 1, 4}); queued = true; };
    for (size_t i = 0; i < g_calLater.size();)
        if (g_calLater[i].second <= now) { if (now < std::max(g_calLater[i].first.end, g_calLater[i].first.start + 1)) queue(g_calLater[i].first); g_calLater.erase(g_calLater.begin() + i); }
        else i++;
    if (g_set.calRemind >= 0)
        for (const CalEvent& e : g_calEvents) {
            if (e.day != today || e.start < 0 || CalReminded(e)) continue;
            int due = std::max(0, e.start - g_set.calRemind);
            if (now < due) continue;
            g_calRemDone.push_back(CalRemKey(e));
            if (now <= e.start + 5) queue(e); // not for events long started (the PC was off)
        }
    if (queued && !g_act) ShowNextReminder();
    ArmCalRemTimer();
}
static void CalRemText(const CalEvent& e, wchar_t* title, wchar_t* sub, int n) {
    int left = e.start - NowMinute();
    if (left > 0) swprintf(title, n, L"In %d min", left);
    else wcscpy(title, L"Starting now");
    swprintf(sub, n, L"%02d:%02d \x00B7 %ls", e.start / 60, e.start % 60, e.title.c_str());
}

// ---- reminders ----
static void ArmTodoTimer() {
    if (g_tasks.empty()) { KillTimer(g_hwnd, TIMER_TODO); return; }
    int now = NowMinute(), today = TodayStamp(), next = 24 * 60; // at the latest, midnight (a new day)
    for (const Task& t : g_tasks) {
        if (TaskDone(t, today) || t.start < 0) continue;
        int due = t.remindAt >= 0 ? t.remindAt : t.start;
        if (g_set.todoStart && t.startShown != today && due > now) next = std::min(next, due);
        if (g_set.todoEnd && t.end >= 0 && t.endShown != today && t.end > now) next = std::min(next, t.end);
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    long long ms = (long long)(next - now) * 60000 - st.wSecond * 1000 - st.wMilliseconds + 300;
    SetTimer(g_hwnd, TIMER_TODO, (UINT)std::max(1000LL, ms), nullptr);
}

static void ShowNextReminder();
static void CheckTaskReminders(int now) {
    int today = TodayStamp();
    RollTasks(today);
    bool changed = false;
    for (int i = 0; i < (int)g_tasks.size(); i++) {
        Task& t = g_tasks[i];
        if (TaskDone(t, today) || t.start < 0) continue;
        int due = t.remindAt >= 0 ? t.remindAt : t.start;
        bool over = t.end >= 0 && now >= t.end;
        if (g_set.todoEnd && over && t.endShown != today) {
            t.endShown = t.startShown = today;
            t.remindAt = -1;
            changed = true;
            if (now - t.end <= 15) g_remQueue.push_back({i, 2}); // not for times long gone (PC was off)
        } else if (g_set.todoStart && !over && t.startShown != today && now >= due) {
            t.startShown = today;
            changed = true;
            if (now - due <= 60) g_remQueue.push_back({i, 1});
        }
    }
    if (changed) SaveTasks();
    if (!g_act) ShowNextReminder();
    ArmTodoTimer();
}

static void ShowNextReminder() {
    int today = TodayStamp();
    while (!g_remQueue.empty()) {
        Reminder r = g_remQueue.front();
        g_remQueue.erase(g_remQueue.begin());
        if (r.kind == 4 ? r.task < 0 || r.task >= (int)g_calRemList.size()
            : r.kind == 3 ? WaterToday() >= g_set.waterGoal : r.task < 0 || r.task >= (int)g_tasks.size() || TaskDone(g_tasks[r.task], today)) continue;
        g_rem = r;
        g_act = true;
        g_toast = true;
        g_flash = true; // stays open until answered
        KillTimer(g_hwnd, TIMER_FLASH);
        wchar_t text[200];
        if (r.kind == 3) { wchar_t s[64]; UiWaterStatus(s, 64); swprintf(text, 200, L"Time for water | %ls so far", s); }
        else if (r.kind == 4) { wchar_t t[96], s[96]; CalRemText(g_calRemList[r.task], t, s, 96); swprintf(text, 200, L"%ls | %ls", t, s); }
        else swprintf(text, 200, L"%ls | %ls", r.kind == 1 ? L"Task Time!" : L"Time's Up!", g_tasks[r.task].title.c_str());
        text[199] = 0;
        if (g_toastAtom) GlobalDeleteAtom(g_toastAtom);
        g_toastAtom = GlobalAddAtomW(text);
        SetPropW(g_hwnd, L"PerchToastAtom", (HANDLE)(ULONG_PTR)g_toastAtom);
        Chime();
        UpdateTarget();
        KickAnim();
        return;
    }
    g_act = false;
    g_rem = {-1, 0};
}

static void AnswerReminder(bool second) { // the pop-up's buttons: Later / Start, +N min / Done
    int today = TodayStamp(), now = NowMinute();
    if (g_rem.kind == 4) { // calendar event: Later (again in N min) / OK
        if (!second && g_rem.task >= 0 && g_rem.task < (int)g_calRemList.size()) { g_calLater.push_back({g_calRemList[g_rem.task], now + g_set.snooze}); ArmCalRemTimer(); }
    } else if (g_rem.kind == 3) { // water: Later / Drank
        if (second) WaterAdd(g_set.waterCup);
        else { g_waterLater = now + g_set.snooze; ArmWaterTimer(); }
    } else if (g_rem.task >= 0 && g_rem.task < (int)g_tasks.size()) {
        Task& t = g_tasks[g_rem.task];
        if (g_rem.kind == 1 && !second) { t.remindAt = now + g_set.snooze; t.startShown = 0; }  // Later
        else if (g_rem.kind == 2 && !second) { t.end = std::min(t.end + g_set.snooze, 24 * 60 - 1); t.endShown = 0; } // +N min
        else if (g_rem.kind == 2 && second) t.doneDay = today;                                    // Done
        SaveTasks();                                                                               // (Start: nothing more)
    }
    g_act = false;
    ShowNextReminder();
    if (!g_act) {
        g_flash = false;
        if (g_hover) { g_toast = false; g_hoverOpen = true; } // back to the page under the pointer
        UpdateTarget();
    }
    ArmTodoTimer();
    KickAnim();
}

// ---- the reminder pop-up (312 x 60, ringed icon, title, task, two buttons) ----
static void ActButtons(const wchar_t** a, const wchar_t** b) {
    static wchar_t more[16];
    swprintf(more, 16, L"+%d min", g_set.snooze);
    *a = g_rem.kind == 2 ? more : L"Later";
    *b = g_rem.kind == 1 ? L"Start" : g_rem.kind == 3 ? L"Drank" : g_rem.kind == 4 ? L"OK" : L"Done";
}
static D2D1_RECT_F ActButtonRect(bool second) {
    const wchar_t *a, *b;
    ActButtons(&a, &b);
    float ax = (kWinW - kActW) / 2, right = ax + 302.4f;
    float wb = TextWidth(b, g_fmtActBtn) + 24, wa = TextWidth(a, g_fmtActBtn) + 24;
    if (second) return D2D1::RectF(right - wb, 16, right, 44);
    return D2D1::RectF(right - wb - 7.2f - wa, 16, right - wb - 7.2f, 44);
}
static void DrawClipboardCheck(D2D1_POINT_2F c, D2D1_COLOR_F col) { // the task icon
    g_brush->SetColor(col);
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - 7.5f, c.y - 7.5f, c.x + 7.5f, c.y + 11), 2.2f, 2.2f), g_brush, 1.8f);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - 4, c.y - 11, c.x + 4, c.y - 6), 1.5f, 1.5f), g_brush);
    g_rt->DrawLine(D2D1::Point2F(c.x - 4, c.y + 2), D2D1::Point2F(c.x - 1, c.y + 5.2f), g_brush, 1.8f, g_round);
    g_rt->DrawLine(D2D1::Point2F(c.x - 1, c.y + 5.2f), D2D1::Point2F(c.x + 4.5f, c.y - 1.2f), g_brush, 1.8f, g_round);
}
static void DrawPageIcon(int page, D2D1_POINT_2F c, D2D1_COLOR_F col);
static void DrawActToast(float a) {
    bool water = g_rem.kind == 3, cal = g_rem.kind == 4;
    if (cal && (g_rem.task < 0 || g_rem.task >= (int)g_calRemList.size())) return;
    if (!water && !cal && (g_rem.task < 0 || g_rem.task >= (int)g_tasks.size())) return;
    float ax = (kWinW - kActW) / 2;
    D2D1_POINT_2F ic = D2D1::Point2F(ax + 40, 30);
    g_brush->SetColor(WithAlpha(cal ? kCalOrange : water ? kWaterBlue : kTodoGreen, 0.3f * a));
    g_rt->DrawEllipse(D2D1::Ellipse(ic, 25.5f, 25.5f), g_brush, 2.4f);
    if (water) {
        D2D1_MATRIX_3X2_F old;
        g_rt->GetTransform(&old);
        g_rt->SetTransform(D2D1::Matrix3x2F::Scale(1.6f, 1.6f, ic) * old);
        DrawPageIcon(PAGE_WATER, ic, WithAlpha(g_fg, a));
        g_rt->SetTransform(old);
    } else if (cal) Text(L"\xE787", g_fmtIcon, D2D1::RectF(ic.x - 14, ic.y - 14, ic.x + 14, ic.y + 14), WithAlpha(g_fg, a)); // Calendar
    else DrawClipboardCheck(ic, WithAlpha(g_fg, a));
    wchar_t sub[96], calTitle[96];
    if (water) { UiWaterStatus(sub, 64); wcscat(sub, L" so far"); }
    if (cal) CalRemText(g_calRemList[g_rem.task], calTitle, sub, 96);
    const wchar_t *ta, *tb;
    ActButtons(&ta, &tb);
    D2D1_RECT_F ra = ActButtonRect(false), rb = ActButtonRect(true);
    float textR = ra.left - 8;
    Text(cal ? calTitle : water ? L"Time for water" : g_rem.kind == 1 ? L"Task Time!" : L"Time's Up!", g_fmtActTitle, D2D1::RectF(ax + 76, 13, textR, 33), WithAlpha(g_fg, a));
    Text(water || cal ? sub : g_tasks[g_rem.task].title.c_str(), g_fmtActSub, D2D1::RectF(ax + 76, 33, textR, 47), WithAlpha(g_grey, a));
    g_brush->SetColor(Rgb(0x262629, a * (g_hot == HOT_ACT_A ? 1.25f : 1)));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(ra, 14, 14), g_brush);
    Text(ta, g_fmtActBtn, ra, WithAlpha(g_fg, a));
    g_brush->SetColor(Rgb(cal ? (g_hot == HOT_ACT_B ? 0x5C3A05 : 0x3D2603) : water ? (g_hot == HOT_ACT_B ? 0x15426E : 0x0D2E4F) : (g_hot == HOT_ACT_B ? 0x0A5A26 : 0x054019), a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(rb, 14, 14), g_brush);
    Text(tb, g_fmtActBtn, rb, Rgb(cal ? 0xFF9F0A : water ? 0x4CADFF : 0x30FF5C, a));
}

// ---- page layout (DIPs from the open pill's left edge) ----
static const float kRowY0 = 60, kRowStep = 30, kRowH = 28;
static const int kRowsShown = 3;
static D2D1_RECT_F TodoPanel() { float px = PX(); return D2D1::RectF(px + 183.2f, 56, px + 364, 147.2f); }
static D2D1_RECT_F TodoAddRect() { float px = PX(); return D2D1::RectF(px + 30.4f, 116, px + 103.2f, 145.6f); } // 72.8 wide
static D2D1_RECT_F TodoEditRect() { float px = PX(); return D2D1::RectF(px + 108, 116, px + 179.2f, 145.6f); }
static D2D1_RECT_F TodoTitleField() { float px = PX(); return D2D1::RectF(px + 188.8f, 64, px + 357.6f, 90.4f); }
static D2D1_RECT_F TodoTimeField() { float px = PX(); return D2D1::RectF(px + 189.6f, 93.6f, px + 356.8f, 117.6f); }
static D2D1_RECT_F TodoDailyChip() { float px = PX(); return D2D1::RectF(px + 316, 97.6f, px + 350, 113.6f); }
static D2D1_RECT_F TodoCancelRect() { float px = PX(); return D2D1::RectF(px + 189.6f, 123.2f, px + 269.6f, 142.4f); }
static D2D1_RECT_F TodoSaveRect() { float px = PX(); return D2D1::RectF(px + 276.8f, 123.2f, px + 356, 142.4f); }
static D2D1_RECT_F TodoRowRect(int row) { // row = index into g_tasks
    float px = PX(), y = kRowY0 + (row - g_todoScroll) * kRowStep;
    return D2D1::RectF(px + 188.8f, y, px + 346.4f, y + kRowH);
}
static D2D1_POINT_2F CenterOf(D2D1_RECT_F r) { return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2); }

// ---- calendar layout (inside the To-do panel) ----
static int CalWeekStart(int day) { return day - (Cal_Weekday(day) - (g_set.weekMonday ? 1 : 0) + 7) % 7; }
static int CalFirstOfMonth(int day) { int y, m, d; Cal_Date(day, y, m, d); return Cal_DayNumber(y, m, 1); }
static int CalCells(int* first) { // number of cells and the day in the first one
    if (g_calWeekView) { *first = CalWeekStart(g_calAnchor); return 7; }
    int f = CalFirstOfMonth(g_calAnchor), y, m, d;
    Cal_Date(f, y, m, d);
    int next = m == 12 ? Cal_DayNumber(y + 1, 1, 1) : Cal_DayNumber(y, m + 1, 1);
    *first = CalWeekStart(f);
    return (next - *first + 6) / 7 * 7;
}
static D2D1_RECT_F CalCellRect(int i) {
    D2D1_RECT_F p = TodoPanel();
    float cw = (p.right - p.left - 12) / 7, x = p.left + 6 + (i % 7) * cw;
    if (g_calWeekView) return D2D1::RectF(x, 87, x + cw, 111);
    int first, rows = CalCells(&first) / 7;
    float rh = (145.6f - 83) / rows, y = 83 + (i / 7) * rh;
    return D2D1::RectF(x, y, x + cw, y + rh);
}
static D2D1_POINT_2F CalArrowCenter(bool next) { D2D1_RECT_F p = TodoPanel(); return D2D1::Point2F(p.right - (next ? 14 : 33), 65.6f); }
static D2D1_RECT_F CalTitleRect() { D2D1_RECT_F p = TodoPanel(); return D2D1::RectF(p.left + 6, 58, p.right - 44, 73); }

// What's on a day: calendar events, then today's tasks (and daily tasks on later days).
struct CalItem { int start, end; const std::wstring* title; bool task; };
static void CalItemsOn(int day, std::vector<CalItem>& out) {
    out.clear();
    for (const CalEvent& e : g_calEvents) if (e.day == day) out.push_back({e.start, e.end, &e.title, false});
    int today = Cal_Today();
    if (day >= today)
        for (const Task& t : g_tasks) if (day == today || t.daily) out.push_back({t.start, t.end, &t.title, true});
    std::stable_sort(out.begin(), out.end(), [](const CalItem& a, const CalItem& b) {
        if (a.task != b.task && (a.start < 0 || b.start < 0)) return !a.task; // tasks with no time go last
        return a.start < b.start;
    });
}
static bool CalHasEvents(int day) {
    for (const CalEvent& e : g_calEvents) if (e.day == day) return true;
    return false;
}
static const int kCalLines = 2; // agenda lines under the date
static D2D1_POINT_2F TodoHotCenter(int hot) {
    float px = PX();
    switch (hot) {
    case HOT_TODO_ADD: return CenterOf(TodoAddRect());
    case HOT_TODO_EDIT: return CenterOf(TodoEditRect());
    case HOT_TODO_CAL: return D2D1::Point2F(px + 118.4f, 70.8f);
    case HOT_TODO_TITLE: return CenterOf(TodoTitleField());
    case HOT_TODO_TIME: { D2D1_RECT_F r = TodoTimeField(); return D2D1::Point2F(r.left + 60, (r.top + r.bottom) / 2); }
    case HOT_TODO_DAILY: return CenterOf(TodoDailyChip());
    case HOT_TODO_CANCEL: return CenterOf(TodoCancelRect());
    case HOT_TODO_SAVE: return CenterOf(TodoSaveRect());
    case HOT_TODO_CLEAR: return D2D1::Point2F(px + 346.4f, 77.2f);
    case HOT_ACT_A: return CenterOf(ActButtonRect(false));
    case HOT_ACT_B: return CenterOf(ActButtonRect(true));
    case HOT_CAL_PREV: return CalArrowCenter(false);
    case HOT_CAL_NEXT: return CalArrowCenter(true);
    case HOT_CAL_TITLE: return CenterOf(CalTitleRect());
    }
    if (hot >= HOT_CAL_DAY0) return CenterOf(CalCellRect(hot - HOT_CAL_DAY0));
    if (hot >= HOT_TODO_DEL0) { D2D1_RECT_F r = TodoRowRect(hot - HOT_TODO_DEL0); return D2D1::Point2F(px + 334, r.top + 13.6f); }
    if (hot >= HOT_TODO_DOWN0) { D2D1_RECT_F r = TodoRowRect(hot - HOT_TODO_DOWN0); return D2D1::Point2F(px + 312, r.top + 13.6f); }
    if (hot >= HOT_TODO_UP0) { D2D1_RECT_F r = TodoRowRect(hot - HOT_TODO_UP0); return D2D1::Point2F(px + 292.4f, r.top + 13.6f); }
    if (hot >= HOT_TODO_ROW0) { D2D1_RECT_F r = TodoRowRect(hot - HOT_TODO_ROW0); return D2D1::Point2F(r.left + 12, r.top + 13.6f); }
    return D2D1::Point2F(-100, -100);
}
static bool InRect(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
static int TodoHit(float x, float y) { // controls of the To-do page or of a reminder pop-up
    if (g_toast && g_act) {
        if (InRect(ActButtonRect(false), x, y)) return HOT_ACT_A;
        if (InRect(ActButtonRect(true), x, y)) return HOT_ACT_B;
        return HOT_NONE;
    }
    if (g_page != PAGE_TODO) return HOT_NONE;
    if (g_todoCal) {
        D2D1_POINT_2F cal = TodoHotCenter(HOT_TODO_CAL);
        if ((x - cal.x) * (x - cal.x) + (y - cal.y) * (y - cal.y) <= 14 * 14) return HOT_TODO_CAL;
        for (int hot : {HOT_CAL_PREV, HOT_CAL_NEXT}) {
            D2D1_POINT_2F c = TodoHotCenter(hot);
            if (fabsf(x - c.x) <= 9 && fabsf(y - c.y) <= 9) return hot;
        }
        if (InRect(CalTitleRect(), x, y)) return HOT_CAL_TITLE;
        int first, n = CalCells(&first);
        for (int i = 0; i < n; i++) if (InRect(CalCellRect(i), x, y)) return HOT_CAL_DAY0 + i;
        return HOT_NONE;
    }
    if (InRect(TodoAddRect(), x, y)) return HOT_TODO_ADD;
    if (InRect(TodoEditRect(), x, y)) return g_tasks.empty() ? HOT_NONE : HOT_TODO_EDIT;
    D2D1_POINT_2F cal = TodoHotCenter(HOT_TODO_CAL);
    if ((x - cal.x) * (x - cal.x) + (y - cal.y) * (y - cal.y) <= 14 * 14) return HOT_TODO_CAL;
    if (g_edit) {
        if (!g_editTitle.empty()) { D2D1_POINT_2F c = TodoHotCenter(HOT_TODO_CLEAR); if (fabsf(x - c.x) < 8 && fabsf(y - c.y) < 8) return HOT_TODO_CLEAR; }
        if (InRect(TodoDailyChip(), x, y)) return HOT_TODO_DAILY;
        if (InRect(TodoTitleField(), x, y)) return HOT_TODO_TITLE;
        if (InRect(TodoTimeField(), x, y)) return HOT_TODO_TIME;
        if (InRect(TodoCancelRect(), x, y)) return HOT_TODO_CANCEL;
        if (InRect(TodoSaveRect(), x, y)) return HOT_TODO_SAVE;
        return HOT_NONE;
    }
    D2D1_RECT_F panel = TodoPanel();
    if (!InRect(panel, x, y)) return HOT_NONE;
    for (int i = g_todoScroll; i < (int)g_tasks.size() && i < g_todoScroll + kRowsShown; i++) {
        D2D1_RECT_F r = TodoRowRect(i);
        if (!InRect(r, x, y)) continue;
        if (g_todoEditMode) {
            for (int base : {HOT_TODO_UP0, HOT_TODO_DOWN0, HOT_TODO_DEL0}) {
                D2D1_POINT_2F c = TodoHotCenter(base + i);
                if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= 9 * 9) return base + i;
            }
        }
        return HOT_TODO_ROW0 + i;
    }
    return HOT_NONE;
}

// ---- editor ----
static void TodoFocus() { // typing goes to the notch while the editor is open
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
}
static void OpenEditor(int idx) { // -1: a new task
    g_edit = idx < 0 ? 1 : 2;
    g_editIdx = idx;
    g_editField = 0;
    g_editBadTitle = g_editBadTime = false;
    g_editTitle.clear();
    g_editTime.clear();
    g_editDaily = false;
    if (idx >= 0) {
        const Task& t = g_tasks[idx];
        g_editTitle = t.title;
        g_editDaily = t.daily;
        wchar_t d[16] = L"";
        if (t.start >= 0) swprintf(d, 16, L"%02d%02d", t.start / 60, t.start % 60);
        if (t.end >= 0) swprintf(d + 4, 12, L"%02d%02d", t.end / 60, t.end % 60);
        g_editTime = d;
    }
    TodoFocus();
}
static void CloseEditor() { g_edit = 0; UpdateTarget(); }
static bool SaveEditor() {
    std::wstring title = g_editTitle;
    while (!title.empty() && iswspace(title.back())) title.pop_back();
    while (!title.empty() && iswspace(title.front())) title.erase(title.begin());
    int start = -1, end = -1;
    const std::wstring& d = g_editTime;
    auto hm = [&](size_t i) { int h = (d[i] - L'0') * 10 + (d[i + 1] - L'0'), m = (d[i + 2] - L'0') * 10 + (d[i + 3] - L'0'); return h < 24 && m < 60 ? h * 60 + m : -2; };
    bool timeOk = d.empty() || d.size() == 4 || d.size() == 8;
    if (timeOk && d.size() >= 4) start = hm(0);
    if (timeOk && d.size() == 8) end = hm(4);
    if (start == -2 || end == -2 || (end >= 0 && end <= start)) timeOk = false;
    g_editBadTitle = title.empty();
    g_editBadTime = !timeOk;
    if (g_editBadTitle || g_editBadTime) return false;
    Task t;
    if (g_edit == 2 && g_editIdx >= 0 && g_editIdx < (int)g_tasks.size()) t = g_tasks[g_editIdx];
    bool timeChanged = t.start != start || t.end != end;
    t.title = title;
    t.start = start;
    t.end = end;
    t.daily = g_editDaily;
    if (timeChanged) { t.startShown = t.endShown = 0; t.remindAt = -1; }
    if (g_edit == 2 && g_editIdx >= 0 && g_editIdx < (int)g_tasks.size()) g_tasks[g_editIdx] = t;
    else g_tasks.push_back(t);
    SaveTasks();
    CloseEditor();
    ArmTodoTimer();
    return true;
}
static void TodoChar(wchar_t c) {
    if (!g_edit) return;
    if (g_editField == 0) {
        if (c >= 32 && c != 127 && g_editTitle.size() < 60) { g_editTitle += c; g_editBadTitle = false; }
    } else if (c >= L'0' && c <= L'9' && g_editTime.size() < 8) {
        g_editTime += c;
        g_editBadTime = false;
    }
    Render();
}
static bool TodoKey(WPARAM vk) { // returns true when handled
    if (!g_edit) return false;
    std::wstring& f = g_editField == 0 ? g_editTitle : g_editTime;
    switch (vk) {
    case VK_BACK: if (!f.empty()) f.pop_back(); break;
    case VK_TAB: g_editField = 1 - g_editField; break;
    case VK_RETURN: SaveEditor(); break;
    case VK_ESCAPE: CloseEditor(); break;
    case 'V':
        if (GetKeyState(VK_CONTROL) < 0 && g_editField == 0 && OpenClipboard(g_hwnd)) { // paste a title
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                    for (; *p && g_editTitle.size() < 60; p++) if (*p >= 32) g_editTitle += *p;
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            break;
        }
        return false;
    default: return false;
    }
    Render();
    return true;
}
static bool TodoHoldOpen() { return g_edit && GetForegroundWindow() == g_hwnd; }

static void TodoClick(int hot) {
    int today = TodayStamp();
    switch (hot) {
    case HOT_ACT_A: AnswerReminder(false); return;
    case HOT_ACT_B: AnswerReminder(true); return;
    case HOT_TODO_ADD: g_todoEditMode = 0; if (g_edit == 1) CloseEditor(); else OpenEditor(-1); return;
    case HOT_TODO_EDIT: g_todoEditMode = !g_todoEditMode; if (g_edit) CloseEditor(); return;
    case HOT_TODO_CAL: // the calendar, opening on today
        g_todoCal = !g_todoCal;
        if (g_todoCal) {
            g_todoEditMode = 0;
            if (g_edit) CloseEditor();
            g_calSel = g_calAnchor = Cal_Today();
            g_calWeekView = g_set.calWeek;
            g_calScroll = 0;
        }
        return;
    case HOT_CAL_PREV: case HOT_CAL_NEXT: {
        int dir = hot == HOT_CAL_NEXT ? 1 : -1;
        if (g_calWeekView) g_calAnchor += 7 * dir;
        else { int y, m, d; Cal_Date(g_calAnchor, y, m, d); m += dir; if (m < 1) { m = 12; y--; } if (m > 12) { m = 1; y++; } g_calAnchor = Cal_DayNumber(y, m, 1); }
        return;
    }
    case HOT_CAL_TITLE: g_calWeekView = !g_calWeekView; g_calAnchor = g_calSel; return;
    case HOT_TODO_TITLE: g_editField = 0; TodoFocus(); return;
    case HOT_TODO_TIME: g_editField = 1; TodoFocus(); return;
    case HOT_TODO_CLEAR: g_editTitle.clear(); g_editField = 0; TodoFocus(); return;
    case HOT_TODO_DAILY: g_editDaily = !g_editDaily; TodoFocus(); return;
    case HOT_TODO_CANCEL: CloseEditor(); return;
    case HOT_TODO_SAVE: SaveEditor(); return;
    }
    if (hot >= HOT_CAL_DAY0) { // a day: show what's on it
        int first;
        CalCells(&first);
        g_calSel = first + hot - HOT_CAL_DAY0;
        if (!g_calWeekView && CalFirstOfMonth(g_calSel) != CalFirstOfMonth(g_calAnchor)) g_calAnchor = g_calSel;
        g_calScroll = 0;
        return;
    }
    if (hot >= HOT_TODO_DEL0) {
        int i = hot - HOT_TODO_DEL0;
        if (i < (int)g_tasks.size()) g_tasks.erase(g_tasks.begin() + i);
        g_todoScroll = std::max(0, std::min(g_todoScroll, (int)g_tasks.size() - kRowsShown));
        if (g_tasks.empty()) g_todoEditMode = 0;
    } else if (hot >= HOT_TODO_DOWN0) {
        int i = hot - HOT_TODO_DOWN0;
        if (i + 1 < (int)g_tasks.size()) std::swap(g_tasks[i], g_tasks[i + 1]);
    } else if (hot >= HOT_TODO_UP0) {
        int i = hot - HOT_TODO_UP0;
        if (i > 0 && i < (int)g_tasks.size()) std::swap(g_tasks[i], g_tasks[i - 1]);
    } else if (hot >= HOT_TODO_ROW0) {
        int i = hot - HOT_TODO_ROW0;
        if (i >= (int)g_tasks.size()) return;
        if (g_todoEditMode) { OpenEditor(i); return; }
        Task& t = g_tasks[i];
        t.doneDay = TaskDone(t, today) ? 0 : today;
    } else return;
    SaveTasks();
    ArmTodoTimer();
}

// ---- drawing ----
static void DrawCheckCircle(D2D1_POINT_2F c, bool done, float a) {
    if (done) {
        g_brush->SetColor(WithAlpha(kTodoGreen, a));
        g_rt->FillEllipse(D2D1::Ellipse(c, 7.2f, 7.2f), g_brush);
        g_brush->SetColor(WithAlpha(g_fg, a));
        g_rt->DrawLine(D2D1::Point2F(c.x - 3.2f, c.y + 0.2f), D2D1::Point2F(c.x - 0.8f, c.y + 2.6f), g_brush, 1.6f, g_round);
        g_rt->DrawLine(D2D1::Point2F(c.x - 0.8f, c.y + 2.6f), D2D1::Point2F(c.x + 3.4f, c.y - 2.4f), g_brush, 1.6f, g_round);
    } else {
        g_brush->SetColor(Rgb(0x8E8E93, a));
        g_rt->DrawEllipse(D2D1::Ellipse(c, 6.6f, 6.6f), g_brush, 1.4f);
    }
}
static void FormatTaskTime(const Task& t, wchar_t* out, size_t n) {
    out[0] = 0;
    if (t.start >= 0 && t.end >= 0) swprintf(out, n, L"%02d:%02d-%02d:%02d", t.start / 60, t.start % 60, t.end / 60, t.end % 60);
    else if (t.start >= 0) swprintf(out, n, L"%02d:%02d", t.start / 60, t.start % 60);
    if (t.daily) wcscat(out, out[0] ? L" \x00B7 Daily" : L"Daily");
}
static void DrawChip(D2D1_RECT_F r, const wchar_t* icon, const wchar_t* label, int state, bool hot, float a) { // 0 normal, 1 active, 2 disabled
    float rad = (r.bottom - r.top) / 2;
    D2D1_COLOR_F bg = state == 1 ? kTodoGreenBg : state == 2 ? Rgb(0x0A0A0A) : Rgb(hot ? 0x262626 : 0x191919);
    D2D1_COLOR_F fg = state == 1 ? kTodoGreen : state == 2 ? Rgb(0x5C5C5C) : g_fg;
    g_brush->SetColor(WithAlpha(bg, a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), g_brush);
    float ix = r.left + (icon ? 14.6f : 15.2f), cy = (r.top + r.bottom) / 2; // icon centres
    if (icon) { // Edit Task: a pencil over a line
        g_brush->SetColor(WithAlpha(fg, a));
        g_rt->DrawLine(D2D1::Point2F(ix - 4.4f, cy + 3), D2D1::Point2F(ix + 4, cy - 5.4f), g_brush, 2.6f, g_round);
        g_rt->FillRectangle(D2D1::RectF(ix - 6.4f, cy + 5.2f, ix + 6.4f, cy + 6.6f), g_brush);
    }
    else { // Add Task: a filled circle with a plus cut out
        g_brush->SetColor(WithAlpha(fg, a));
        g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ix, cy), 6.4f, 6.4f), g_brush);
        g_brush->SetColor(WithAlpha(bg, a));
        g_rt->FillRectangle(D2D1::RectF(ix - 3, cy - 0.7f, ix + 3, cy + 0.7f), g_brush);
        g_rt->FillRectangle(D2D1::RectF(ix - 0.7f, cy - 3, ix + 0.7f, cy + 3), g_brush);
    }
    Text(label, g_fmtTodoBtn, D2D1::RectF(ix + (icon ? 12.8f : 11.6f), r.top, r.right, r.bottom), WithAlpha(fg, a));
}
static void DrawCalButton(float a) {
    D2D1_POINT_2F cal = TodoHotCenter(HOT_TODO_CAL);
    g_brush->SetColor(g_todoCal ? WithAlpha(kTodoGreenBg, a) : Rgb(g_hot == HOT_TODO_CAL ? 0x2A2A2A : 0x1F1F1F, a));
    g_rt->FillEllipse(D2D1::Ellipse(cal, 13.6f, 13.6f), g_brush);
    Text(L"\xE787", g_fmtBarIcon, D2D1::RectF(cal.x - 10, cal.y - 10, cal.x + 10, cal.y + 10), g_todoCal ? WithAlpha(kTodoGreen, a) : WithAlpha(g_grey, a)); // Calendar
    if (g_todoCal) return;
    g_brush->SetColor(WithAlpha(g_bg, a)); // a blue dot, with a black edge
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cal.x + 10.4f, cal.y - 8.4f), 3.8f, 3.8f), g_brush);
    g_brush->SetColor(Rgb(0x006DFF, a));
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cal.x + 10.4f, cal.y - 8.4f), 2.8f, 2.8f), g_brush);
}

static const D2D1_COLOR_F kCalBlue = {0.26f, 0.52f, 0.96f, 1}; // Google Calendar's blue (#4285F4) for events

// The calendar view of the To-do page.
static void DrawTodoCalendar(float a) {
    float px = PX();
    int today = Cal_Today();
    D2D1_COLOR_F fg = WithAlpha(g_fg, a), grey = WithAlpha(g_grey, a);
    static const wchar_t* kDays[7] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
    static const wchar_t* kMonths[12] = {L"January", L"February", L"March", L"April", L"May", L"June", L"July",
                                         L"August", L"September", L"October", L"November", L"December"};

    // Left: the selected day's date, big like the task count, and its weekday
    int y, m, d;
    Cal_Date(g_calSel, y, m, d);
    wchar_t buf[96];
    swprintf(buf, 96, L"%d", d);
    Text(buf, g_fmtTodoCount, D2D1::RectF(px + 31, 52.8f, px + 104, 89.8f), g_calSel == today ? WithAlpha(kTodoGreen, a) : fg);
    DrawCalButton(a);
    const wchar_t* rel = g_calSel == today ? L"Today" : g_calSel == today + 1 ? L"Tomorrow" : g_calSel == today - 1 ? L"Yesterday" : nullptr;
    if (rel) swprintf(buf, 96, L"%ls \x00B7 %ls", kDays[Cal_Weekday(g_calSel)], rel);
    else swprintf(buf, 96, L"%ls, %.3ls", kDays[Cal_Weekday(g_calSel)], kMonths[m - 1]);
    Text(buf, g_fmtMeta, D2D1::RectF(px + 31.2f, 90, px + 180, 105.6f), grey);

    // ...and what's on it
    std::vector<CalItem> items;
    CalItemsOn(g_calSel, items);
    g_calScroll = std::max(0, std::min(g_calScroll, (int)items.size() - kCalLines));
    if (items.empty()) Text(g_calStatus == CAL_CHECKING ? L"Checking your calendar" : L"Nothing scheduled", g_fmtCalLine,
                            D2D1::RectF(px + 31.2f, 110, px + 178, 124), WithAlpha(Rgb(0x6E6E73), a));
    for (int i = 0; i < kCalLines && g_calScroll + i < (int)items.size(); i++) {
        const CalItem& it = items[g_calScroll + i];
        float ly = 110 + i * 17.6f;
        if (i == kCalLines - 1 && (int)items.size() > g_calScroll + kCalLines) { // more than fits
            swprintf(buf, 96, L"+%d more", (int)items.size() - g_calScroll - kCalLines + 1);
            Text(buf, g_fmtCalLine, D2D1::RectF(px + 31.2f, ly, px + 178, ly + 14), grey);
            break;
        }
        g_brush->SetColor(WithAlpha(it.task ? kTodoGreen : kCalBlue, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px + 31.6f, ly + 1.5f, px + 34.4f, ly + 12.5f), 1.4f, 1.4f), g_brush);
        wchar_t tm[24];
        if (it.start >= 0 && it.end >= 0) swprintf(tm, 24, L"%02d:%02d-%02d:%02d", it.start / 60, it.start % 60, it.end / 60, it.end % 60);
        else if (it.start >= 0) swprintf(tm, 24, L"%02d:%02d", it.start / 60, it.start % 60);
        else wcscpy(tm, it.task ? L"Daily" : L"All day");
        if (it.task && it.start < 0 && !(g_calSel > today)) wcscpy(tm, L"Task");
        float tw = TextWidth(tm, g_fmtCalLine);
        Text(tm, g_fmtCalLine, D2D1::RectF(px + 39, ly, px + 39 + tw + 1, ly + 14), grey);
        Text(it.title->c_str(), g_fmtCalLine, D2D1::RectF(px + 39 + tw + 5, ly, px + 178, ly + 14), fg);
    }

    // Right: the month (or week)
    D2D1_RECT_F panel = TodoPanel();
    g_brush->SetColor(Rgb(0x131313, a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(panel, 13, 13), g_brush);
    Cal_Date(g_calWeekView ? CalWeekStart(g_calAnchor) : g_calAnchor, y, m, d);
    swprintf(buf, 96, L"%ls %d", kMonths[m - 1], y);
    D2D1_RECT_F tr = CalTitleRect();
    if (g_hot == HOT_CAL_TITLE) {
        g_brush->SetColor(Rgb(0x222222, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tr.left, tr.top, tr.left + TextWidth(buf, g_fmtCalHead) + 30, tr.bottom), 6, 6), g_brush);
    }
    Text(buf, g_fmtCalHead, D2D1::RectF(tr.left + 5, tr.top, tr.right, tr.bottom), fg);
    Text(g_calWeekView ? L"Week" : L"Month", g_fmtCalWd, D2D1::RectF(tr.left + TextWidth(buf, g_fmtCalHead) + 7, tr.top, tr.left + TextWidth(buf, g_fmtCalHead) + 29, tr.bottom),
         WithAlpha(kTodoGreen, a));
    for (int hot : {HOT_CAL_PREV, HOT_CAL_NEXT}) {
        D2D1_POINT_2F c = TodoHotCenter(hot);
        g_brush->SetColor(Rgb(g_hot == hot ? 0x333333 : 0x222222, a));
        g_rt->FillEllipse(D2D1::Ellipse(c, 7.6f, 7.6f), g_brush);
        Text(hot == HOT_CAL_PREV ? L"\xE76B" : L"\xE76C", g_fmtChevron, D2D1::RectF(c.x - 8, c.y - 8, c.x + 8, c.y + 8), WithAlpha(g_fg, 0.8f * a)); // ChevronLeft / Right
    }
    static const wchar_t* kLetters = L"SMTWTFS";
    int first, n = CalCells(&first), month = m;
    for (int i = 0; i < 7; i++) { // weekday letters
        D2D1_RECT_F c = CalCellRect(i);
        wchar_t l[2] = {kLetters[(i + (g_set.weekMonday ? 1 : 0)) % 7], 0};
        float top = g_calWeekView ? 75 : 73.5f;
        Text(l, g_fmtCalWd, D2D1::RectF(c.left, top, c.right, top + 9), WithAlpha(Rgb(0x8E8E93), a));
    }
    for (int i = 0; i < n; i++) {
        int day = first + i, dy, dm, dd;
        Cal_Date(day, dy, dm, dd);
        D2D1_RECT_F c = CalCellRect(i);
        D2D1_POINT_2F ctr = CenterOf(c);
        float r = g_calWeekView ? 9.6f : std::min(5.6f, (c.bottom - c.top) / 2);
        bool sel = day == g_calSel, isToday = day == today, out = !g_calWeekView && dm != month;
        if (isToday && sel) { g_brush->SetColor(WithAlpha(kTodoGreen, a)); g_rt->FillEllipse(D2D1::Ellipse(ctr, r + 1.4f, r + 0.4f), g_brush); }
        else if (sel) { g_brush->SetColor(Rgb(0x3A3A3C, a)); g_rt->FillEllipse(D2D1::Ellipse(ctr, r + 1.4f, r + 0.4f), g_brush); }
        else if (g_hot == HOT_CAL_DAY0 + i) { g_brush->SetColor(Rgb(0x262626, a)); g_rt->FillEllipse(D2D1::Ellipse(ctr, r + 1.4f, r + 0.4f), g_brush); }
        swprintf(buf, 96, L"%d", dd);
        D2D1_COLOR_F col = isToday && sel ? Rgb(0x0B2A12, a) : isToday ? WithAlpha(kTodoGreen, a) : out ? Rgb(0x48484A, a) : fg;
        Text(buf, g_calWeekView ? g_fmtCalWeekNum : isToday ? g_fmtCalNumB : g_fmtCalNum, c, col);
        if (CalHasEvents(day)) { // a dot for days with events
            g_brush->SetColor(out ? Rgb(0x2C4A7A, a) : WithAlpha(kCalBlue, a));
            float dotY = g_calWeekView ? c.bottom + 4 : ctr.y + r - 0.6f;
            g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(g_calWeekView ? ctr.x : ctr.x + r + 0.6f, g_calWeekView ? dotY : ctr.y - r + 1.2f), 1.1f, 1.1f), g_brush);
        }
    }
    if (g_calWeekView) { // under the week: how much is on it
        int ev = 0;
        for (const CalEvent& e : g_calEvents) if (e.day >= first && e.day < first + 7) ev++;
        if (g_calStatus == CAL_NONE) wcscpy(buf, L"Add a calendar link: right-click the notch");
        else swprintf(buf, 96, ev == 1 ? L"1 event this week" : L"%d events this week", ev);
        g_fmtPomoFoot->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(buf, g_fmtPomoFoot, D2D1::RectF(panel.left + 8, 122, panel.right - 8, 140), WithAlpha(Rgb(0x8E8E93), a));
        g_fmtPomoFoot->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

static void DrawTodoPage(float a) {
    float px = PX();
    int today = TodayStamp(), done = 0;
    for (const Task& t : g_tasks) done += TaskDone(t, today);
    D2D1_COLOR_F fg = WithAlpha(g_fg, a), grey = WithAlpha(g_grey, a);

    // title: "Todo" + green "List"
    D2D1_RECT_F tr = D2D1::RectF(px + 32, 21.6f, px + 160, 41.6f);
    Text(L"Todo", g_fmtHdr, tr, fg);
    tr.left += TextWidth(L"Todo", g_fmtHdr);
    Text(L"List", g_fmtHdr, tr, WithAlpha(kTodoGreen, a));
    if (g_todoCal) { DrawTodoCalendar(a); return; }

    wchar_t buf[64];
    swprintf(buf, 64, L"%d/%d", done, (int)g_tasks.size());
    Text(buf, g_fmtTodoCount, D2D1::RectF(px + 31, 52.8f, px + 104, 89.8f), fg);
    DrawCalButton(a);
    Text(L"Task Done Today", g_fmtMeta, D2D1::RectF(px + 31.2f, 97, px + 180, 112.6f), grey);
    DrawChip(TodoAddRect(), nullptr, L"Add Task", g_edit == 1 ? 1 : 0, g_hot == HOT_TODO_ADD, a);
    DrawChip(TodoEditRect(), L"\xE70F", L"Edit Task", g_tasks.empty() ? 2 : (g_todoEditMode || g_edit == 2) ? 1 : 0, g_hot == HOT_TODO_EDIT, a); // Edit (pencil)

    D2D1_RECT_F panel = TodoPanel();
    g_brush->SetColor(Rgb(0x131313, a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(panel, 13, 13), g_brush);

    if (g_edit) {
        bool focused = GetForegroundWindow() == g_hwnd;
        // title field
        D2D1_RECT_F f = TodoTitleField();
        g_brush->SetColor(Rgb(0x080808, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(f, 8, 8), g_brush);
        D2D1_COLOR_F edge = g_editBadTitle ? kRed : (g_editField == 0 && focused) ? Rgb(0xD0D0D0) : Rgb(0x3A3A3A);
        g_brush->SetColor(WithAlpha(edge, a));
        g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(f.left + 0.75f, f.top + 0.75f, f.right - 0.75f, f.bottom - 0.75f), 8, 8), g_brush, 1.5f);
        g_brush->SetColor(WithAlpha(kTodoGreen, a));
        g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(f.left + 12, (f.top + f.bottom) / 2), 4, 4), g_brush);
        D2D1_RECT_F tx = D2D1::RectF(f.left + 25.6f, f.top, f.right - 22, f.bottom);
        if (g_editTitle.empty()) Text(L"Task title", g_fmtTodoField, tx, Rgb(0x6E6E73, a));
        else {
            Text(g_editTitle.c_str(), g_fmtTodoField, tx, fg);
            D2D1_POINT_2F c = TodoHotCenter(HOT_TODO_CLEAR); // clear button
            g_brush->SetColor(Rgb(g_hot == HOT_TODO_CLEAR ? 0xB0B0B0 : 0x8E8E93, a));
            g_rt->FillEllipse(D2D1::Ellipse(c, 6.4f, 6.4f), g_brush);
            g_brush->SetColor(Rgb(0x080808, a));
            g_rt->DrawLine(D2D1::Point2F(c.x - 2.4f, c.y - 2.4f), D2D1::Point2F(c.x + 2.4f, c.y + 2.4f), g_brush, 1.4f, g_round);
            g_rt->DrawLine(D2D1::Point2F(c.x + 2.4f, c.y - 2.4f), D2D1::Point2F(c.x - 2.4f, c.y + 2.4f), g_brush, 1.4f, g_round);
        }
        if (g_editField == 0 && focused) { // caret after the text
            float cx = tx.left + (g_editTitle.empty() ? 0 : std::min(TextWidth(g_editTitle.c_str(), g_fmtTodoField), tx.right - tx.left)) + 1;
            g_brush->SetColor(fg);
            g_rt->FillRectangle(D2D1::RectF(cx, f.top + 7, cx + 1.2f, f.bottom - 7), g_brush);
        }
        // time field: __:__ - __:__
        D2D1_RECT_F tf = TodoTimeField();
        g_brush->SetColor(Rgb(0x222226, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(tf, 8, 8), g_brush);
        if (g_editBadTime || (g_editField == 1 && focused)) {
            g_brush->SetColor(WithAlpha(g_editBadTime ? kRed : Rgb(0xD0D0D0), a));
            g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tf.left + 0.75f, tf.top + 0.75f, tf.right - 0.75f, tf.bottom - 0.75f), 8, 8), g_brush, 1.5f);
        }
        Text(L"\xE823", g_fmtBarIcon, D2D1::RectF(tf.left + 2, tf.top, tf.left + 22, tf.bottom), fg); // Clock
        wchar_t mask[16] = L"__:__ - __:__";
        static const int slot[8] = {0, 1, 3, 4, 8, 9, 11, 12};
        for (size_t i = 0; i < g_editTime.size() && i < 8; i++) mask[slot[i]] = g_editTime[i];
        if (g_editTime.size() == 8) // complete: a compact "15:53-15:54"
            swprintf(mask, 16, L"%c%c:%c%c-%c%c:%c%c", g_editTime[0], g_editTime[1], g_editTime[2], g_editTime[3], g_editTime[4], g_editTime[5], g_editTime[6], g_editTime[7]);
        Text(mask, g_fmtTodoField, D2D1::RectF(tf.left + 25.6f, tf.top, tf.right - 40, tf.bottom), g_editTime.empty() ? Rgb(0x6E6E73, a) : fg);
        D2D1_RECT_F dc = TodoDailyChip();
        g_brush->SetColor(g_editDaily ? WithAlpha(kTodoGreenBg, a) : Rgb(0x333336, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(dc, 8, 8), g_brush);
        Text(L"Daily", g_fmtPomoBtn, dc, g_editDaily ? WithAlpha(kTodoGreen, a) : Rgb(0xD0D0D0, a));
        // Cancel / Save
        D2D1_RECT_F cr = TodoCancelRect(), sr = TodoSaveRect();
        g_brush->SetColor(Rgb(g_hot == HOT_TODO_CANCEL ? 0x2C2C31 : 0x212126, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(cr, 9.6f, 9.6f), g_brush);
        Text(L"Cancel", g_fmtTodoSmall, cr, fg);
        g_brush->SetColor(WithAlpha(g_hot == HOT_TODO_SAVE ? kTodoGreen : kSaveGreen, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(sr, 9.6f, 9.6f), g_brush);
        Text(L"Save", g_fmtTodoSmall, sr, Rgb(0x0B2A12, a));
        return;
    }

    if (g_tasks.empty()) {
        float cx = px + 273.2f;
        D2D1_RECT_F box = D2D1::RectF(cx - 16.4f, 73.6f, cx + 16.4f, 106.4f);
        g_brush->SetColor(Rgb(0x444444, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(box, 8, 8), g_brush);
        g_brush->SetColor(Rgb(0x131313, a));
        for (int i = 0; i < 2; i++) { // a checklist: two ticks and two lines
            float y = 84 + i * 11;
            g_rt->DrawLine(D2D1::Point2F(cx - 9, y), D2D1::Point2F(cx - 6.5f, y + 2.5f), g_brush, 1.8f, g_round);
            g_rt->DrawLine(D2D1::Point2F(cx - 6.5f, y + 2.5f), D2D1::Point2F(cx - 2.5f, y - 2), g_brush, 1.8f, g_round);
            g_rt->DrawLine(D2D1::Point2F(cx + 1.5f, y + 0.5f), D2D1::Point2F(cx + 9, y + 0.5f), g_brush, 1.8f, g_round);
        }
        Text(L"No tasks.", g_fmtTodoSmall, D2D1::RectF(cx - 60, 115, cx + 60, 130), grey);
        return;
    }

    g_rt->PushAxisAlignedClip(panel, D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = g_todoScroll; i < (int)g_tasks.size() && i < g_todoScroll + kRowsShown; i++) {
        const Task& t = g_tasks[i];
        bool isDone = TaskDone(t, today);
        D2D1_RECT_F r = TodoRowRect(i);
        if (isDone || g_hot == HOT_TODO_ROW0 + i) {
            g_brush->SetColor(isDone ? WithAlpha(kTodoGreenBg, a) : Rgb(0x1E1E1E, a));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), g_brush);
        }
        DrawCheckCircle(D2D1::Point2F(r.left + 12, r.top + 13.6f), isDone, a);
        wchar_t tm[48];
        FormatTaskTime(t, tm, 48);
        float textR = g_todoEditMode ? px + 282 : r.right - 4;
        D2D1_RECT_F title = tm[0] ? D2D1::RectF(r.left + 24.8f, r.top + 3, textR, r.top + 15.5f) : D2D1::RectF(r.left + 24.8f, r.top, textR, r.bottom);
        Text(t.title.c_str(), g_fmtTodoRow, title, isDone ? grey : fg);
        if (isDone) { // struck through
            float w = std::min(TextWidth(t.title.c_str(), g_fmtTodoRow), textR - title.left), y = (title.top + title.bottom) / 2 + 0.5f;
            g_brush->SetColor(grey);
            g_rt->FillRectangle(D2D1::RectF(title.left, y - 0.5f, title.left + w, y + 0.5f), g_brush);
        }
        if (tm[0]) Text(tm, g_fmtTodoTime, D2D1::RectF(r.left + 25.6f, r.top + 17, textR, r.top + 26.5f), grey);
        if (g_todoEditMode) {
            for (int k = 0; k < 3; k++) {
                int hot = (k == 0 ? HOT_TODO_UP0 : k == 1 ? HOT_TODO_DOWN0 : HOT_TODO_DEL0) + i;
                D2D1_POINT_2F c = TodoHotCenter(hot);
                g_brush->SetColor(k == 2 ? Rgb(g_hot == hot ? 0xFF4A44 : 0xE53833, a) : Rgb(g_hot == hot ? 0x3A3A3A : 0x2A2A2A, a));
                g_rt->FillEllipse(D2D1::Ellipse(c, 8.4f, 8.4f), g_brush);
                Text(k == 0 ? L"\xE70E" : k == 1 ? L"\xE70D" : L"\xE711", g_fmtChevron, D2D1::RectF(c.x - 8, c.y - 8, c.x + 8, c.y + 8),
                     WithAlpha(g_fg, (k == 2 ? 1.0f : 0.75f) * a)); // ChevronUp / ChevronDown / Cancel
            }
        }
    }
    g_rt->PopAxisAlignedClip();
}

// =====================================================================
// Mascot (a little friend that lives in the notch):
// after 15 s without mouse or keyboard input the closed pill fades into Perch's falcon, peeking
// through the notch over a teal-blue-purple glow. It looks around, blinks once, and after 7 s falls
// asleep (eyes shut, dimmer glow, one "z" floats up), then stays still. Any input wakes it: its eyes
// go wide as the glow flashes, then a blink, then it fades back to the clock 7 s later.
// Event-driven: a one-shot timer for the 15 s, raw input only while the face shows, and frames
// only while something moves (asleep it is a still picture).
// =====================================================================
enum PetMode { PET_OFF = 0, PET_AWAKE, PET_SLEEP, PET_GREET };
static int g_petMode = PET_OFF;
static ULONGLONG g_petAt;       // when g_petMode began
static float g_petFromH = 11.2f; // eye height when the greeting began (shut eyes open from a line)
static bool g_petOffEnv = false; // PERCH_MASCOT_OFF set (tests that look at the clock pill)
static bool g_petInput = false;  // raw input registered
static const DWORD kPetIdleMs = 15000;
static const float kPetAwakeS = 7.05f, kPetGreetS = 7.6f;

static float PetSec() { return (GetTickCount64() - g_petAt) / 1000.0f; }
static DWORD InputIdleMs() {
    LASTINPUTINFO li = {sizeof(li)};
    return GetLastInputInfo(&li) ? GetTickCount() - li.dwTime : 0;
}
static bool PetWanted() { // the clock pill may turn into the mascot (nothing else to show; not with the Minimal tab)
    return g_set.mascot && !g_set.minimal && !g_petOffEnv && !(g_media.hasSession && g_media.playing) && g_pomoState != POMO_RUNNING;
}
static bool PetCanAppear() { return PetWanted() && !g_hidden && g_t == 0 && g_target == 0 && !g_toast && !g_dragOver; }
static void PetListen(bool on) { // raw mouse and keyboard input, from any app, while the face shows
    if (on == g_petInput) return;
    RAWINPUTDEVICE rid[2] = {{1, 2, on ? (DWORD)RIDEV_INPUTSINK : (DWORD)RIDEV_REMOVE, on ? g_hwnd : nullptr},
                             {1, 6, on ? (DWORD)RIDEV_INPUTSINK : (DWORD)RIDEV_REMOVE, on ? g_hwnd : nullptr}};
    if (RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)) || !on) g_petInput = on;
}
static float PetAlpha() { // 0 = the clock pill, 1 = the mascot
    if (g_petMode == PET_OFF) return 0;
    float t = PetSec();
    auto rise = [](float s) { return s <= 0 ? 0.0f : 1 - (1 + 8 * s) * expf(-8 * s); }; // the glow's rise
    if (g_petMode == PET_AWAKE) return rise(t);
    if (g_petMode == PET_GREET) return 1 - rise(t - 6.75f);
    return 1;
}
static void PetFrames(bool on) {
    if (!on) { KillTimer(g_hwnd, TIMER_PET_ANIM); return; }
    // As few frames as the motion needs: 60 a second for the fades, 30 for the blink, the wave and the
    // eyes closing, 15 for the slow breathing / looking around and the drifting "z".
    float t = PetSec();
    int ms = 66;
    if (g_petMode == PET_AWAKE) ms = t < 0.9f ? 16 : (t > 2.9f && t < 3.6f) || t > kPetAwakeS - 0.4f ? 33 : 66;
    else if (g_petMode == PET_GREET) ms = t < 1.2f || t > 6.6f ? 16 : t < 5.2f ? 33 : 66;
    SetTimer(g_hwnd, TIMER_PET_ANIM, ms, nullptr);
}
static void ArmPetIdle() { // one timer for the moment the user will have been away 15 s
    KillTimer(g_hwnd, TIMER_PET_IDLE);
    if (g_petMode != PET_OFF || !PetWanted()) return;
    DWORD idle = InputIdleMs();
    SetTimer(g_hwnd, TIMER_PET_IDLE, idle >= kPetIdleMs ? 50 : kPetIdleMs - idle + 50, nullptr);
}
static void PetStart() {
    g_petMode = PET_AWAKE;
    g_petAt = GetTickCount64();
    PetListen(true);
    PetFrames(true);
}
static void PetStop() {
    g_petMode = PET_OFF;
    PetListen(false);
    PetFrames(false);
    ArmPetIdle();
}
static void PetWake() { // input while the face shows
    if (g_petMode != PET_AWAKE && g_petMode != PET_SLEEP) return;
    g_petFromH = g_petMode == PET_SLEEP ? 2.2f : 11.2f;
    g_petMode = PET_GREET;
    g_petAt = GetTickCount64();
    PetListen(false);
    PetFrames(true);
}
static void PetRecheck() { // something changed (music, Pomodoro, settings, notch closed): show or hide the mascot
    if (g_petMode != PET_OFF && !PetWanted()) { PetStop(); if (!g_hidden) Render(); }
    else if (g_petMode == PET_OFF) ArmPetIdle();
}
static void OnPetIdleTimer() {
    KillTimer(g_hwnd, TIMER_PET_IDLE);
    if (g_petMode != PET_OFF || !PetWanted()) return;
    DWORD idle = InputIdleMs();
    if (idle < kPetIdleMs) { SetTimer(g_hwnd, TIMER_PET_IDLE, kPetIdleMs - idle + 50, nullptr); return; }
    if (PetCanAppear()) PetStart(); // else: the notch is open or a pop-up shows; closing it looks again
}
static void OnPetFrame() {
    float t = PetSec();
    if (g_petMode == PET_AWAKE && t >= kPetAwakeS) { g_petMode = PET_SLEEP; g_petAt = GetTickCount64(); }
    else if (g_petMode == PET_GREET && t >= kPetGreetS) { PetStop(); }
    else if (g_petMode == PET_SLEEP && t > 2.7f) PetFrames(false); // the "z" is gone: a still picture from here
    else if (g_petMode == PET_OFF) PetFrames(false);
    else PetFrames(true); // re-pick the frame rate
    if (!g_hidden && !g_animating && g_t < 0.25f) Render(); // open: the face is hidden, nothing to draw
}

static float SmoothStep(float v) { v = Clamp01(v); return v * v * (3 - 2 * v); }

// The face inside the closed pill `shape`, at opacity a.
static void DrawMascot(ID2D1Geometry* shape, D2D1_RECT_F p, float a, float shown) {
    if (a <= 0) return;
    float fa = a * SmoothStep((shown - 0.15f) / 0.5f); // the falcon arrives after the glow
    float top = 0, bottom = p.bottom, cx = (p.left + p.right) / 2, cy = bottom / 2 + 0.5f;
    float t = PetSec();

    // Glow brightness (1 = the flash on waking) and eyes: open height (11.2 open, 2.2 shut, more = wide), looking around.
    float glow = 0.85f, eyeH = 11.2f, look = 0;
    if (g_petMode == PET_AWAKE) {
        glow = 0.87f * (0.866f + 0.138f * cosf(6.2832f * (t - 1) / 5)); // breathing, 5 s
        look = 3.2f * cosf(6.2832f * (t - 1.1f) / 7.4f);
        float b = (t - 3.1f) / 0.35f; // one blink
        if (b > 0 && b < 1) eyeH = Lerp(11.2f, 2.2f, 1 - fabsf(b * 2 - 1));
        if (t > kPetAwakeS - 0.25f) { float c = SmoothStep((t - (kPetAwakeS - 0.25f)) / 0.25f); eyeH = Lerp(11.2f, 2.2f, c); glow = Lerp(glow, 0.5f, c); }
    } else if (g_petMode == PET_SLEEP) {
        glow = 0.5f;
        eyeH = 2.2f;
    } else if (g_petMode == PET_GREET) {
        glow = 0.85f + 0.15f * expf(-t / 0.35f);
        if (t < 0.46f) eyeH = Lerp(g_petFromH, 15.2f, SmoothStep(t / 0.46f)); // spots you: eyes go wide
        else if (t < 0.75f) eyeH = Lerp(15.2f, 12, (t - 0.46f) / 0.29f);
        else eyeH = Lerp(12, 11.2f, Clamp01((t - 0.75f) / 0.6f));
        float b = (t - 4.75f) / 0.3f; // a blink
        if (b > 0 && b < 1) eyeH = Lerp(11.2f, 2.2f, 1 - fabsf(b * 2 - 1));
    }

    // Black under it (Glass), then the glow in the style's colours along the bottom, fading to black upwards.
    // The glow's colours never change, only its brightness, so it is drawn once into a bitmap (per notch
    // size) and each frame only places that bitmap at the right opacity.
    g_brush->SetColor(D2D1::ColorF(0, 0, 0, a));
    g_rt->FillGeometry(shape, g_brush);
    D2D1_RECT_F gb;
    shape->GetBounds(nullptr, &gb);
    gb.top = top;
    D2D1_COLOR_F glowCol[3];
    float glowStrength = 1;
    Mascot_Glow(g_set.mascotStyle, glowCol, &glowStrength);
    float key[5] = {roundf(gb.left * 4), roundf(gb.right * 4), roundf(gb.bottom * 4), g_scale, (float)g_set.mascotStyle};
    if (!g_petGlow || memcmp(key, g_petGlowKey, sizeof key) != 0) {
        SafeRelease(g_petGlow);
        memcpy(g_petGlowKey, key, sizeof key);
        ID2D1BitmapRenderTarget* bt = nullptr;
        if (SUCCEEDED(g_rt->CreateCompatibleRenderTarget(D2D1::SizeF(gb.right - gb.left, gb.bottom - gb.top), &bt))) {
            ID2D1GradientStopCollection* stops = nullptr;
            ID2D1LinearGradientBrush* lg = nullptr;
            bt->BeginDraw();
            bt->Clear(D2D1::ColorF(0, 0, 0, 0));
            bt->SetTransform(D2D1::Matrix3x2F::Translation(-gb.left, -gb.top));
            D2D1_GRADIENT_STOP hs[3] = {{0, glowCol[0]}, {0.5f, glowCol[1]}, {1, glowCol[2]}};
            if (SUCCEEDED(bt->CreateGradientStopCollection(hs, 3, &stops)) &&
                SUCCEEDED(bt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(p.left, 0), D2D1::Point2F(p.right, 0)), stops, &lg)))
                bt->FillGeometry(shape, lg);
            SafeRelease(lg);
            SafeRelease(stops);
            D2D1_GRADIENT_STOP vs[5] = {{0, D2D1::ColorF(0, 0, 0, 1)}, {0.3f, D2D1::ColorF(0, 0, 0, 0.94f)}, {0.55f, D2D1::ColorF(0, 0, 0, 0.7f)},
                                        {0.8f, D2D1::ColorF(0, 0, 0, 0.33f)}, {1, D2D1::ColorF(0, 0, 0, 0)}};
            if (SUCCEEDED(bt->CreateGradientStopCollection(vs, 5, &stops)) &&
                SUCCEEDED(bt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, top), D2D1::Point2F(0, bottom)), stops, &lg)))
                bt->FillGeometry(shape, lg);
            SafeRelease(lg);
            SafeRelease(stops);
            if (SUCCEEDED(bt->EndDraw())) bt->GetBitmap(&g_petGlow);
            bt->Release();
        }
    }
    if (g_petGlow) g_rt->DrawBitmap(g_petGlow, gb, a * Clamp01(glow * 1.03f) * glowStrength, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    // The face itself (mascot.cpp): the falcon fills the notch, cut off by its outline, its beak's tip poking out below;
    // the other styles are a few shapes around the eyes' line.
    Mascot_DrawFace(g_rt, g_d2d, g_brush, g_set.mascotStyle, shape, cx, bottom, fa, (eyeH - 2.2f) / 9.0f, look, t);

    // Asleep: one "z" drifts up and fades.
    if (g_petMode == PET_SLEEP && t > 0.3f && t < 2.5f) {
        float z = (t - 0.3f) / 2.2f;
        float za = z < 0.4f ? z / 0.4f : 1 - (z - 0.4f) / 0.6f;
        float zx = cx + 23.2f + 4 * z, zy = cy - 3.2f - 4.8f * z;
        Text(L"z", g_fmtPetZ, D2D1::RectF(zx - 6, zy - 6, zx + 6, zy + 6), D2D1::ColorF(0.86f, 0.87f, 0.92f, fa * 0.72f * za));
    }
}

// Water page: today's amount and the goal on the left; on the right the next reminder, "+ cup" and undo.
static void DrawWaterPage(float a) {
    float px = PX();
    D2D1_COLOR_F fg = WithAlpha(g_fg, a), grey = WithAlpha(g_grey, a);
    Text(L"Water", g_fmtHdr, D2D1::RectF(px + 32, 21.6f, px + 120, 41.6f), fg);
    wchar_t buf[64], amt[16], goal[16];
    int ml = WaterToday();
    FormatLitres(ml, amt, 16);
    FormatLitres(g_set.waterGoal, goal, 16);
    Text(L"Today", g_fmtPomoSmall, D2D1::RectF(px + 34, 50, px + 178, 64.4f), grey);
    Text(amt, g_fmtPomoTime, D2D1::RectF(px + 33, 66, px + 180, 102), fg);
    int cups = WaterCups(), had = std::min(cups, (ml + g_set.waterCup / 2) / g_set.waterCup);
    swprintf(buf, 64, L"of %ls \x00B7 %d of %d cups", goal, had, cups);
    Text(buf, g_fmtPomoSub, D2D1::RectF(px + 34, 108, px + 180, 127), fg);
    D2D1_RECT_F track = D2D1::RectF(px + 34, 133, px + 162, 137);
    g_brush->SetColor(WithAlpha(g_fg, 0.14f * a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), g_brush);
    float f = WaterFraction();
    if (f > 0) {
        D2D1_RECT_F fill = track;
        fill.right = track.left + (track.right - track.left) * f;
        g_brush->SetColor(WithAlpha(kWaterBlue, a));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 2, 2), g_brush);
    }

    g_brush->SetColor(WithAlpha(g_fg, 0.1f * a));
    g_rt->FillRectangle(D2D1::RectF(px + 180.5f, 54, px + 181.5f, 140), g_brush);

    Text(L"Next reminder", g_fmtPomoSmall, D2D1::RectF(px + 194, 53.2f, px + 364, 69.2f), grey);
    int due = WaterNextDue(NowMinute());
    if (ml >= g_set.waterGoal) wcscpy(buf, L"Goal reached");
    else if (!g_set.waterRemind) wcscpy(buf, L"Reminders off");
    else if (due < 0) wcscpy(buf, L"None left today");
    else swprintf(buf, 64, L"%02d:%02d", due / 60, due % 60);
    Text(buf, g_fmtPomoSub, D2D1::RectF(px + 194, 68, px + 364, 87), ml >= g_set.waterGoal ? WithAlpha(kWaterBlue, a) : fg);

    D2D1_RECT_F add = WaterAddRect();
    g_brush->SetColor(WithAlpha(kWaterBlue, a * (g_hot == HOT_WATER_ADD ? 1.0f : 0.85f)));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(add, 13, 13), g_brush);
    wchar_t cup[24];
    FormatLitres(g_set.waterCup, buf, 16);
    swprintf(cup, 24, L"+ %ls", buf);
    Text(cup, g_fmtPomoBtn, add, Rgb(0x0B1E33, a));
    D2D1_POINT_2F uc = HotCenter(HOT_WATER_UNDO);
    g_brush->SetColor(WithAlpha(g_hot == HOT_WATER_UNDO ? g_fg : g_chip, (g_hot == HOT_WATER_UNDO ? 0.22f : 1.0f) * a));
    g_rt->FillEllipse(D2D1::Ellipse(uc, 11, 11), g_brush);
    g_brush->SetColor(WithAlpha(g_fg, (ml > 0 ? 0.85f : 0.3f) * a)); // a minus: take one cup back
    g_rt->FillRectangle(D2D1::RectF(uc.x - 4.5f, uc.y - 0.8f, uc.x + 4.5f, uc.y + 0.8f), g_brush);

    swprintf(buf, 64, L"Active %02d:%02d\x2013%02d:%02d", g_set.waterFrom / 60, g_set.waterFrom % 60, g_set.waterUntil / 60, g_set.waterUntil % 60);
    Text(buf, g_fmtPomoFoot, D2D1::RectF(px + 194, 124.5f, px + 370, 139.5f), grey);
}

static void DrawComingSoonPage(int page, float a);
static void DrawPage(int page, float a, float dx) {
    if (a <= 0.01f) return;
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(dx - g_baseX, g_offY));
    if (page == PAGE_MUSIC) DrawMusicPage(a);
    else if (page == PAGE_POMODORO) DrawPomodoroPage(a);
    else if (page == PAGE_TODO) DrawTodoPage(a);
    else if (page == PAGE_WATER) DrawWaterPage(a);
    else if (page == PAGE_SHELF) {
        Text(L"Shelf", g_fmtHdr, D2D1::RectF(PX() + 32, 21.6f, PX() + 110, 41.6f), WithAlpha(g_fg, a));
        if (ClipActive() && !g_dragOver) DrawClipList(a);
        else DrawShelf(a);
        if (g_set.clipHistory && !g_dragOver) DrawShelfTabs(a);
    } else DrawComingSoonPage(page, a);
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(-g_baseX, g_offY));
}

// Page icons: thin outlines, about 12 DIPs.
static void DrawPageIcon(int page, D2D1_POINT_2F c, D2D1_COLOR_F col);

// Pages Perch doesn't do yet: the page's icon, its name and "Coming soon".
static void DrawComingSoonPage(int page, float a) {
    float px = PX(), cx = px + kExpW / 2;
    const wchar_t* what = page == PAGE_WATER ? L"Reminders to drink water through the day" : L"A page for the widgets you pick";
    Text(kPageTitles[page], g_fmtHdr, D2D1::RectF(px + 32, 21.6f, px + 200, 41.6f), WithAlpha(g_fg, a));
    g_brush->SetColor(Rgb(0x1C1C1E, a));
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, 78), 20, 20), g_brush);
    D2D1_MATRIX_3X2_F old;
    g_rt->GetTransform(&old);
    g_rt->SetTransform(D2D1::Matrix3x2F::Scale(1.7f, 1.7f, D2D1::Point2F(cx, 78)) * old);
    DrawPageIcon(page, D2D1::Point2F(cx, 78), Rgb(kPageColor[page], a));
    g_rt->SetTransform(old);
    g_fmtPomoSub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(what, g_fmtPomoSub, D2D1::RectF(px + 40, 104, px + kExpW - 40, 124), WithAlpha(g_fg, a));
    g_fmtPomoSub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    Text(L"Coming soon", g_fmtMeta, D2D1::RectF(px + 40, 124, px + kExpW - 40, 142), WithAlpha(g_grey, a));
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

static void DrawPageIcon(int page, D2D1_POINT_2F c, D2D1_COLOR_F col) {
    g_brush->SetColor(col);
    const float sw = 1.5f;
    auto line = [&](float x1, float y1, float x2, float y2) { g_rt->DrawLine(D2D1::Point2F(c.x + x1, c.y + y1), D2D1::Point2F(c.x + x2, c.y + y2), g_brush, sw, g_round); };
    switch (page) {
    case PAGE_MUSIC: // eighth note
        g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - 2.2f, c.y + 3.6f), 2.7f, 2.2f), g_brush);
        line(0.3f, 3.4f, 0.3f, -5.4f);
        line(0.3f, -5.4f, 3.8f, -3.2f);
        break;
    case PAGE_POMODORO: // stopwatch
        g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + 1), 4.8f, 4.8f), g_brush, sw);
        line(-1.8f, -5.8f, 1.8f, -5.8f);
        line(0, -5.8f, 0, -3.8f);
        line(0, 1, 0, -1.6f);
        break;
    case PAGE_SHELF: { // inbox tray
        ID2D1PathGeometry* path = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(g_d2d->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
            D2D1_POINT_2F pts[] = {{c.x - 4.6f, c.y - 4.4f}, {c.x - 6.2f, c.y - 0.2f}, {c.x - 6.2f, c.y + 4.6f}, {c.x + 6.2f, c.y + 4.6f},
                                   {c.x + 6.2f, c.y - 0.2f}, {c.x + 4.6f, c.y - 4.4f}};
            sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
            for (int i = 1; i < 6; i++) sink->AddLine(pts[i]);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            D2D1_POINT_2F in[] = {{c.x - 6.2f, c.y + 0.6f}, {c.x - 2.6f, c.y + 0.6f}, {c.x - 1.8f, c.y + 2.2f}, {c.x + 1.8f, c.y + 2.2f},
                                  {c.x + 2.6f, c.y + 0.6f}, {c.x + 6.2f, c.y + 0.6f}};
            sink->BeginFigure(in[0], D2D1_FIGURE_BEGIN_HOLLOW);
            for (int i = 1; i < 6; i++) sink->AddLine(in[i]);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            g_rt->DrawGeometry(path, g_brush, sw, g_round);
        }
        SafeRelease(sink);
        SafeRelease(path);
        break;
    }
    case PAGE_TODO: // checklist
        for (int i = 0; i < 2; i++) {
            float y = i ? 2.8f : -2.8f;
            line(-6.4f, y, -5, y + 1.4f);
            line(-5, y + 1.4f, -2.4f, y - 1.6f);
            line(0.6f, y, 6.4f, y);
        }
        break;
    case PAGE_WATER: { // drop
        ID2D1PathGeometry* path = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(g_d2d->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(c.x, c.y - 6), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + 1.6f, c.y - 3.4f), D2D1::Point2F(c.x + 4.6f, c.y - 0.6f), D2D1::Point2F(c.x + 4.6f, c.y + 1.8f)));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(c.x - 4.6f, c.y + 1.8f), D2D1::SizeF(4.6f, 4.6f), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - 4.6f, c.y - 0.6f), D2D1::Point2F(c.x - 1.6f, c.y - 3.4f), D2D1::Point2F(c.x, c.y - 6)));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            g_rt->DrawGeometry(path, g_brush, sw, g_round);
        }
        SafeRelease(sink);
        SafeRelease(path);
        break;
    }
    case PAGE_WIDGETS: // four tiles
        for (int i = 0; i < 4; i++) {
            float x = (i % 2 ? 1.0f : -5.4f), y = (i / 2 ? 1.0f : -5.4f);
            g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x + x, c.y + y, c.x + x + 4.4f, c.y + y + 4.4f), 1.2f, 1.2f), g_brush, 1.3f);
        }
        break;
    default: // settings
        Text(L"\xE713", g_fmtBarIcon, D2D1::RectF(c.x - 12, c.y - 12, c.x + 12, c.y + 12), col);
    }
}

// Arrows at the sides; dots along the bottom, which turn into page icons (and settings) on hover.
static void DrawPageNav(float a) {
    for (int hot : {HOT_ARROW_L, HOT_ARROW_R}) {
        DrawHotCircle(hot, a);
        D2D1_POINT_2F c = HotCenter(hot);
        Text(hot == HOT_ARROW_L ? L"\xE76B" : L"\xE76C", g_fmtChevron, D2D1::RectF(c.x - 12, c.y - 12, c.x + 12, c.y + 12),
             WithAlpha(g_fg, (g_hot == hot ? 0.95f : 0.45f) * a));
    }
    float cx = kWinW / 2, dots = a * (1 - g_iconBar);
    if (dots > 0.01f) {
        float total = (g_pageN - 1) * 8.0f + 14;
        float x = cx - total / 2;
        for (int i = 0; i < g_pageN; i++) {
            bool cur = g_pages[i] == g_page;
            float wdt = cur ? 14.0f : 4.0f;
            g_brush->SetColor(WithAlpha(g_fg, (cur ? 0.95f : 0.4f) * dots));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, kDotsY - 2, x + wdt, kDotsY + 2), 2, 2), g_brush);
            x += wdt + 4;
        }
    }
    float bar = a * g_iconBar;
    if (bar > 0.01f) {
        D2D1_POINT_2F first = HotCenter(HOT_ICON0), last = HotCenter(HOT_ICON_SETTINGS);
        D2D1_RECT_F bg = D2D1::RectF(first.x - 18, first.y - 10.8f, last.x + 17.6f, first.y + 10.8f); // 21.6 tall
        g_brush->SetColor(Rgb(0x131313, bar));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(bg, 10.8f, 10.8f), g_brush);
        for (int i = 0; i <= g_pageN; i++) {
            int hot = i == g_pageN ? HOT_ICON_SETTINGS : HOT_ICON0 + i, p = i == g_pageN ? -1 : g_pages[i];
            D2D1_COLOR_F col = p >= 0 && p == g_page ? Rgb(kPageColor[p]) : g_hot == hot ? g_fg : Rgb(0x8E8E93);
            DrawPageIcon(p, HotCenter(hot), WithAlpha(col, bar));
        }
        float divX = (HotCenter(HOT_ICON0 + g_pageN - 1).x + last.x) / 2;
        g_brush->SetColor(Rgb(0x48484A, bar));
        g_rt->FillRectangle(D2D1::RectF(divX - 0.5f, first.y - 6.4f, divX + 0.5f, first.y + 6.4f), g_brush);
    }
}

// The notch outline. Classic: straight sides, rounded bottom corners (radius r), and concave
// shoulders (radius sh) that curve out to meet the top edge. Floating: a rounded rect.
static ID2D1PathGeometry* PillGeometry(float x, float w, float h, float r, float sh, bool island, bool openTop = false) {
    ID2D1PathGeometry* path = nullptr;
    ID2D1GeometrySink* k = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&path))) return nullptr;
    if (FAILED(path->Open(&k))) { path->Release(); return nullptr; }
    r = std::min(r, std::min(h / 2, w / 2));
    auto arc = [&](float ex, float ey, float rad, bool cw) {
        k->AddArc(D2D1::ArcSegment(D2D1::Point2F(ex, ey), D2D1::SizeF(rad, rad), 0,
                                   cw ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    if (island) {
        k->BeginFigure(D2D1::Point2F(x + r, 0), D2D1_FIGURE_BEGIN_FILLED);
        k->AddLine(D2D1::Point2F(x + w - r, 0));
        arc(x + w, r, r, true);
        k->AddLine(D2D1::Point2F(x + w, h - r));
        arc(x + w - r, h, r, true);
        k->AddLine(D2D1::Point2F(x + r, h));
        arc(x, h - r, r, true);
        k->AddLine(D2D1::Point2F(x, r));
        arc(x + r, 0, r, true);
    } else {
        sh = std::min(sh, std::max(0.0f, h - r));
        k->BeginFigure(D2D1::Point2F(x - sh, 0), D2D1_FIGURE_BEGIN_FILLED);
        if (sh > 0) arc(x, sh, sh, true);     // left shoulder
        k->AddLine(D2D1::Point2F(x, h - r));
        arc(x + r, h, r, false);              // bottom left
        k->AddLine(D2D1::Point2F(x + w - r, h));
        arc(x + w, h - r, r, false);          // bottom right
        k->AddLine(D2D1::Point2F(x + w, sh));
        if (sh > 0) arc(x + w + sh, 0, sh, true); // right shoulder
    }
    k->EndFigure(openTop && !island ? D2D1_FIGURE_END_OPEN : D2D1_FIGURE_END_CLOSED); // open: an outline without the top edge
    k->Close();
    k->Release();
    return path;
}

// ---- Ask Perch: questions to Gemini with your own key ----
// The shortcut (Ctrl + Alt + P unless changed; right-click > Ask Perch too) opens the notch on a question field; Enter sends it on a
// worker thread (ask.cpp) and the answer shows under it, scrollable. Esc or switching away closes it.
// The key is kept encrypted for this Windows account (DPAPI) in ask.key, never in settings.ini. The model
// is the user's choice: Settings lists the models their key can use.
static bool g_ask = false;                 // the Ask panel is showing
static bool g_askBusy = false;             // a question is on its way
static std::wstring g_askInput, g_askQuestion, g_askAnswer, g_askError;
static std::vector<AskTurn> g_askTurns;    // the conversation so far (kept short)
static float g_askScroll = 0;              // DIPs scrolled into the answer
static int g_askCopied = 0;                // "Copied" shows (until the timer)
static std::vector<std::wstring> g_askModels; // what the key may use, from Google
static bool g_askModelsBusy = false, g_askModelsTried = false; // asked Google once for this key
struct AskJob { int kind; std::wstring key, model; std::vector<AskTurn> turns; int status = 0; std::wstring answer, error; std::vector<std::wstring> models; };
static std::wstring AskKeyFile() { return DataDir() + L"\\ask.key"; }
static std::wstring AskKey() {
    std::wstring key;
    FILE* f = _wfopen(AskKeyFile().c_str(), L"rb");
    if (!f) return key;
    std::vector<BYTE> enc;
    BYTE buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) enc.insert(enc.end(), buf, buf + n);
    fclose(f);
    DATA_BLOB in = {(DWORD)enc.size(), enc.data()}, out = {};
    if (!enc.empty() && CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        key.assign((const wchar_t*)out.pbData, out.cbData / sizeof(wchar_t));
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
    return key;
}
static bool AskSaveKey(const std::wstring& key) {
    DATA_BLOB in = {(DWORD)(key.size() * sizeof(wchar_t)), (BYTE*)key.data()}, out = {};
    if (!CryptProtectData(&in, L"Perch Gemini key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    bool ok = false;
    if (FILE* f = _wfopen(AskKeyFile().c_str(), L"wb")) { ok = fwrite(out.pbData, 1, out.cbData, f) == out.cbData; fclose(f); }
    LocalFree(out.pbData);
    return ok;
}
static bool AskHasKey() { return GetFileAttributesW(AskKeyFile().c_str()) != INVALID_FILE_ATTRIBUTES; }
static DWORD WINAPI AskThread(LPVOID p) {
    AskJob* j = (AskJob*)p;
    if (j->kind == 0) j->status = Ask_Generate(j->key, j->model, j->turns, j->answer, j->error);
    else j->status = Ask_ListModels(j->key, j->models, j->error);
    SecureZeroMemory(&j->key[0], j->key.size() * sizeof(wchar_t));
    if (!PostMessageW(g_hwnd, WM_APP_ASK_DONE, 0, (LPARAM)j)) delete j;
    return 0;
}
static void AskStart(AskJob* j) {
    HANDLE t = CreateThread(nullptr, 64 * 1024, AskThread, j, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) CloseHandle(t);
    else delete j;
}
static void AskFetchModels() { // the models this key can use, for the Settings list
    if (g_askModelsBusy || !AskHasKey()) return;
    g_askModelsBusy = g_askModelsTried = true;
    AskStart(new AskJob{1, AskKey()});
}
static void AskSend() {
    std::wstring q = g_askInput;
    while (!q.empty() && iswspace(q.back())) q.pop_back();
    while (!q.empty() && iswspace(q.front())) q.erase(q.begin());
    if (q.empty() || g_askBusy) return;
    if (!AskHasKey()) { g_askError = L"Add your Gemini key in Settings > Ask Perch first."; Render(); return; }
    g_askQuestion = q;
    g_askInput.clear();
    g_askAnswer.clear();
    g_askError.clear();
    g_askScroll = 0;
    g_askBusy = true;
    g_askTurns.push_back({true, q});
    if (g_askTurns.size() > 12) g_askTurns.erase(g_askTurns.begin(), g_askTurns.end() - 12); // the last few exchanges
    AskStart(new AskJob{0, AskKey(), g_set.askModel, g_askTurns});
    Render();
}
static void OnAskDone(AskJob* j) {
    if (j->kind == 1) {
        g_askModelsBusy = false;
        if (j->status == 200) g_askModels = j->models;
        SettingsUI_Refresh(g_set);
    } else {
        g_askBusy = false;
        if (j->status == 200) { g_askAnswer = j->answer; g_askTurns.push_back({false, j->answer}); }
        else { g_askError = j->error; if (!g_askTurns.empty() && g_askTurns.back().user) g_askTurns.pop_back(); }
        UpdateTarget();
        if (!g_hidden) Render();
    }
    delete j;
}
static void AskFocus() { SetForegroundWindow(g_hwnd); SetFocus(g_hwnd); }
static void AskOpen() {
    g_ask = true;
    g_askCopied = 0;
    if (g_edit) CloseEditor();
    AskFocus();
    UpdateTarget();
    KickAnim();
    Render();
}
static void AskClose() { g_ask = false; UpdateTarget(); KickAnim(); if (!g_hidden) Render(); }
static bool AskHoldOpen() { return g_ask && (GetForegroundWindow() == g_hwnd || g_askBusy); }
static void AskCopyAnswer() {
    if (g_askAnswer.empty() || !OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (g_askAnswer.size() + 1) * sizeof(wchar_t))) {
        memcpy(GlobalLock(m), g_askAnswer.c_str(), (g_askAnswer.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(m);
        if (!SetClipboardData(CF_UNICODETEXT, m)) GlobalFree(m);
    }
    CloseClipboard();
    g_clipOwnSeq = GetClipboardSequenceNumber(); // not into clipboard history
    g_askCopied = 1;
    SetTimer(g_hwnd, TIMER_ASKCOPY, 1400, nullptr);
}
static void AskChar(wchar_t c) {
    if (c >= 32 && c != 127 && g_askInput.size() < 2000) g_askInput += c;
    Render();
}
static bool AskKeyDown(WPARAM vk) {
    if (!g_ask) return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    switch (vk) {
    case VK_BACK: if (!g_askInput.empty()) g_askInput.pop_back(); break;
    case VK_RETURN: AskSend(); return true;
    case VK_ESCAPE: AskClose(); return true;
    case VK_UP: case VK_PRIOR: g_askScroll = std::max(0.0f, g_askScroll - (vk == VK_UP ? 20.0f : 60.0f)); break;
    case VK_DOWN: case VK_NEXT: g_askScroll += vk == VK_DOWN ? 20.0f : 60.0f; break;
    case 'V':
        if (!ctrl || !OpenClipboard(g_hwnd)) return false;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                for (; *p && g_askInput.size() < 2000; p++) g_askInput += *p == L'\r' || *p == L'\n' || *p == L'\t' ? L' ' : *p;
                GlobalUnlock(h);
            }
        CloseClipboard();
        break;
    case 'C': if (!ctrl) return false; AskCopyAnswer(); break;
    default: return false;
    }
    Render();
    return true;
}
static D2D1_RECT_F AskFieldRect() { float px = PX(); return D2D1::RectF(px + 30, 48, px + kExpW - 30, 74); }
static D2D1_RECT_F AskAnswerRect() { float px = PX(); return D2D1::RectF(px + 34, 82, px + kExpW - 34, 146); }
static D2D1_RECT_F AskCopyRect() { float px = PX(); return D2D1::RectF(px + kExpW - 92, 148, px + kExpW - 34, 164); }
static void DrawAsk(float a) {
    float px = PX();
    D2D1_COLOR_F fg = WithAlpha(g_fg, a), grey = WithAlpha(g_grey, a);
    Text(L"Ask Perch", g_fmtHdr, D2D1::RectF(px + 32, 21.6f, px + 200, 41.6f), fg);
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    Text(g_set.askModel.c_str(), g_fmtMeta, D2D1::RectF(px + 200, 23, px + kExpW - 34, 39), grey);
    g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    D2D1_RECT_F f = AskFieldRect();
    g_brush->SetColor(WithAlpha(g_fg, 0.08f * a));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(f, 13, 13), g_brush);
    D2D1_RECT_F tr = D2D1::RectF(f.left + 12, f.top + 5, f.right - 12, f.bottom - 5);
    bool typing = GetForegroundWindow() == g_hwnd;
    std::wstring shown = g_askInput;
    // keep the end of a long question in view
    while (shown.size() > 1 && TextWidth(shown.c_str(), g_fmtMeta) > tr.right - tr.left - 8) shown.erase(0, 1);
    if (shown.empty()) Text(g_askBusy ? L"" : L"Ask anything\x2026", g_fmtMeta, tr, grey);
    else Text(shown.c_str(), g_fmtMeta, tr, fg);
    if (typing && !g_askBusy) { // the caret
        float cx = tr.left + (shown.empty() ? 0 : TextWidth(shown.c_str(), g_fmtMeta)) + 1;
        g_brush->SetColor(WithAlpha(kWaterBlue, a));
        g_rt->FillRectangle(D2D1::RectF(cx, tr.top + 1, cx + 1.2f, tr.bottom - 1), g_brush);
    }

    D2D1_RECT_F ar = AskAnswerRect();
    std::wstring body;
    D2D1_COLOR_F col = fg;
    if (g_askBusy) { body = L"Thinking\x2026"; col = grey; }
    else if (!g_askError.empty()) { body = g_askError; col = WithAlpha(kRed, a); }
    else if (!g_askAnswer.empty()) body = g_askAnswer;
    else if (!AskHasKey()) { body = L"Ask Perch uses Google's Gemini with your own free key. Add it in Settings > Ask Perch."; col = grey; }
    else { body = L"Type a question and press Enter. Esc closes."; col = grey; }
    if (!g_askQuestion.empty() && (g_askBusy || !g_askAnswer.empty() || !g_askError.empty())) body = g_askQuestion + L"\n\n" + body;
    IDWriteTextLayout* l = nullptr;
    if (SUCCEEDED(g_dw->CreateTextLayout(body.c_str(), (UINT32)body.size(), g_fmtAsk, ar.right - ar.left, 10000, &l))) {
        DWRITE_TEXT_METRICS m;
        l->GetMetrics(&m);
        float maxScroll = std::max(0.0f, m.height - (ar.bottom - ar.top));
        g_askScroll = std::min(g_askScroll, maxScroll);
        if (!g_askQuestion.empty() && (g_askBusy || !g_askAnswer.empty() || !g_askError.empty())) {
            DWRITE_TEXT_RANGE q = {0, (UINT32)g_askQuestion.size()};
            l->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, q);
        }
        g_rt->PushAxisAlignedClip(ar, D2D1_ANTIALIAS_MODE_ALIASED);
        g_brush->SetColor(col);
        g_rt->DrawTextLayout(D2D1::Point2F(ar.left, ar.top - g_askScroll), l, g_brush);
        g_rt->PopAxisAlignedClip();
        if (maxScroll > 0) { // where you are in it
            float th = (ar.bottom - ar.top) * (ar.bottom - ar.top) / m.height, ty = ar.top + (ar.bottom - ar.top - th) * g_askScroll / maxScroll;
            g_brush->SetColor(WithAlpha(g_fg, 0.25f * a));
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ar.right + 6, ty, ar.right + 9, ty + th), 1.5f, 1.5f), g_brush);
        }
        l->Release();
    }
    if (!g_askAnswer.empty() && !g_askBusy) {
        g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(g_askCopied ? L"Copied" : L"Copy answer", g_fmtMeta, AskCopyRect(), WithAlpha(g_hot == HOT_ASK_COPY ? g_fg : g_grey, a));
        g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    if (!AskHasKey()) {
        g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(L"Open Settings", g_fmtMeta, AskCopyRect(), WithAlpha(g_hot == HOT_ASK_SETTINGS ? g_fg : kWaterBlue, a));
        g_fmtMeta->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}
static void UiAskStatus(wchar_t* out, int n) {
    if (!AskHasKey()) wcsncpy(out, L"Not set", n);
    else if (g_askModelsBusy) wcsncpy(out, L"Saved \x00B7 checking models\x2026", n);
    else if (g_askModels.empty() && g_askModelsTried) wcsncpy(out, L"Saved \x00B7 couldn't load the models (offline?)", n);
    else swprintf(out, n, g_askModels.empty() ? L"Saved" : L"Saved \x00B7 %d models", (int)g_askModels.size());
    out[n - 1] = 0;
}
static void UiAskModels(std::vector<std::wstring>& out) {
    out = g_askModels;
    if (out.empty() && AskHasKey() && !g_askModelsTried) AskFetchModels();
}
static void AskPasteKey() {
    std::wstring key;
    if (OpenClipboard(g_hwnd)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { key = p; GlobalUnlock(h); }
        CloseClipboard();
    }
    key.erase(std::remove_if(key.begin(), key.end(), [](wchar_t c) { return iswspace(c); }), key.end());
    bool looks = key.size() >= 30 && key.size() <= 200 && std::all_of(key.begin(), key.end(), [](wchar_t c) { return iswalnum(c) || c == L'-' || c == L'_'; });
    if (!looks) { ShowToast(L"No key on the clipboard", L"Copy your key from aistudio.google.com, then paste again.", L"\xE8BD"); return; }
    if (!AskSaveKey(key)) { ShowToast(L"Couldn't save the key", L"Check that Perch can write to its folder.", L"\xE8BD"); return; }
    SecureZeroMemory(&key[0], key.size() * sizeof(wchar_t));
    g_askModels.clear();
    g_askError.clear();
    g_askModelsTried = false;
    AskFetchModels();
    ShowToast(L"Gemini key saved", L"Encrypted for your Windows account", L"\xE8BD");
    SettingsUI_Refresh(g_set);
}
static void AskRemoveKey() {
    DeleteFileW(AskKeyFile().c_str());
    g_askModels.clear();
    g_askModelsTried = false;
    SettingsUI_Refresh(g_set);
}
static bool g_askHotkeyOk = true; // false: the shortcut belongs to another app
static void AskHotkey() { // follow the setting: (re)register the shortcut when it changes
    static std::wstring now; // what's registered
    static bool on = false;
    if (g_set.askHotkey == now) return;
    if (on) UnregisterHotKey(g_hwnd, kAskHotkeyId);
    on = false;
    now = g_set.askHotkey;
    UINT mods, vk;
    if (Settings_ParseHotkey(now, mods, vk)) on = RegisterHotKey(g_hwnd, kAskHotkeyId, mods | MOD_NOREPEAT, vk) != 0;
    g_askHotkeyOk = on || !_wcsicmp(now.c_str(), L"Off");
    SettingsUI_Refresh(g_set);
}
static bool UiAskHotkeyOk() { return g_askHotkeyOk; }

static void Render() {
    if (!g_rt && !CreateSurface()) return;

    float e = g_t;
    if (g_cw <= 0) { g_cw = CollapsedWidth(); g_ch = CollapsedHeight(); }
    bool tab = g_ch < 12; // Minimal tab (or on its way there)
    float closedH = g_ch + g_peek * (tab ? 2.4f : 4.8f), closedW = g_cw + g_peek * 10;
    float closedR = std::min((float)g_set.roundness, closedH * 0.4f), closedSh = std::min(kShoulderClosed, closedH * 0.3f);
    float w = Lerp(closedW, OpenWidth(), e), r = Lerp(closedR, g_toast && g_act ? kActR : kExpR, e);
    float h = Lerp(closedH, OpenHeight(), e);
    if (e < 0) { // closing dips just under the closed size and springs back (the tab: 44 x 4 -> 39 x 1.6 -> 44 x 4)
        w = closedW + e * std::min(OpenWidth() - closedW, closedW * 7);
        h = std::max(0.5f, closedH + e * std::min(OpenHeight() - closedH, closedH * 40));
    }
    float x = PillX(w);
    float content = Clamp01((e - 0.55f) / 0.45f); // expanded content fades in late
    float pillContent = Clamp01(1 - e * 4);       // collapsed content fades out early
    bool island = g_set.style == STYLE_ISLAND;
    g_pill = D2D1::RectF(x, 0, x + w, h);

    EnsureArt();
    EnsureShelfIcons();
    // Only the notch's own area (and where it was last frame) is redrawn and handed to Windows: the window is
    // 440 x 250 DIPs, the closed pill a fraction of that, so the mascot and the clock cost little.
    float shMax = std::max(Lerp(closedSh, kShoulderOpen, e), 0.0f);
    D2D1_RECT_F now = D2D1::RectF(std::min(x - shMax, kWinW / 2 - 75) - 2, 0, std::max(x + w + shMax, kWinW / 2 + 75) + 2, g_offY + std::max(h, 10.0f) + 2);
    if (g_petMode != PET_OFF) now.bottom = std::max(now.bottom, g_offY + h * 70 / 56 + 2); // the falcon's beak pokes out below
    if (content > 0) { // the page, its dots, arrows and icon bar sit at their open places while they fade, outside a shrinking notch
        float ow = OpenWidth(), ox = PillX(ow);
        now.left = std::min(now.left, ox - kShoulderOpen - 2);
        now.right = std::max(now.right, ox + ow + kShoulderOpen + 2);
        now.bottom = std::max(now.bottom, g_offY + std::max(OpenHeight(), kExpH + kIconBarH) + 2); // the icon bar, also while it slides away
    }
    D2D1_RECT_F dirty = g_fullUpdate ? D2D1::RectF(0, 0, kWinW, kWinH)
                                     : D2D1::RectF(std::min(now.left, g_lastDrawn.left), 0, std::max(now.right, g_lastDrawn.right), std::max(now.bottom, g_lastDrawn.bottom));
    RECT dpx = {std::max(0L, (LONG)std::floor(dirty.left * g_scale)), 0, std::min((LONG)g_pxW, (LONG)std::ceil(dirty.right * g_scale)),
                std::min((LONG)g_pxH, (LONG)std::ceil(dirty.bottom * g_scale))};
    if (dpx.right <= dpx.left || dpx.bottom <= dpx.top) dpx = {0, 0, g_pxW, g_pxH};
    g_lastDrawn = now;
    // The render target is bound to just that area of the window's bitmap, so Direct2D only rasterises and
    // copies those pixels; drawing is shifted left by its offset.
    g_baseX = dpx.left / g_scale;
    g_rt->BindDC(g_memDC, &dpx);
    g_rt->BeginDraw();
    g_rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(-g_baseX, g_offY)); // island: everything sits a little below the edge

    // Pill. Classic: a rounded rect pushed above the screen edge so only the bottom corners show.
    // Floating: all four corners show.
    ID2D1PathGeometry* shape = PillGeometry(x, w, h, r, Lerp(closedSh, kShoulderOpen, e), island);
    if (h < 20) { // the Minimal tab is tiny: an invisible (alpha 1/255) strip still catches the pointer
        g_brush->SetColor(D2D1::ColorF(0, 0, 0, 1.5f / 255));
        g_rt->FillRectangle(D2D1::RectF(kWinW / 2 - 75, 0, kWinW / 2 + 75, 10), g_brush);
    }
    if (shape) {
        DrawGlassBehind(shape);
        g_brush->SetColor(g_bg);
        g_rt->FillGeometry(shape, g_brush);
        if (g_edge.a > 0) { // Glass: a thin light edge, but not along the top of a Classic notch
            if (ID2D1PathGeometry* edge = PillGeometry(x, w, h, r, Lerp(closedSh, kShoulderOpen, e), island, true)) {
                g_brush->SetColor(g_edge);
                g_rt->DrawGeometry(edge, g_brush, 1.0f);
                edge->Release();
            }
        }
    }

    // ---- collapsed indicators ----
    // The status ring on the left and the time, bold, in the middle; sound bars on the right while playing.
    pillContent *= Clamp01((g_ch - 12) / 14); // the Minimal tab has no clock
    float pet = PetAlpha();
    if (pillContent > 0 && pet > 0 && shape) DrawMascot(shape, D2D1::RectF(x, 0, x + w, h), pillContent * pet, pet);
    // The clock dims to ~70% while the glow comes up, and goes once the face is nearly there.
    pillContent *= 1 - 0.3f * SmoothStep(pet / 0.45f) - 0.7f * SmoothStep((pet - 0.85f) / 0.13f);
    if (pillContent > 0) {
        float cy = h / 2;
        if (g_media.hasSession && g_media.playing) {
            static const float pb[5] = {2.4f, 7.2f, 10.4f, 8, 2.4f};
            DrawSoundBars(D2D1::Point2F(x + w - 22.2f, cy), pb, 4.2f, pillContent); // 52.8 right of the centre
        }
        {
            bool ring = (g_set.ring == RING_BATTERY && g_battPct >= 0) || g_set.ring == RING_TASKS || g_set.ring == RING_WATER;
            bool small = w < 130; // Minimal: ring and time sit closer together
            float ringX = x + (small ? 18 : 20); // ring centre 20 from the pill's left edge (55 left of the centre)
            if (ring && g_set.ring == RING_TASKS) DrawRingArc(D2D1::Point2F(ringX, cy), TasksDoneFraction(), kRingGreen, pillContent);
            else if (ring && g_set.ring == RING_WATER) DrawRingArc(D2D1::Point2F(ringX, cy), WaterFraction(), kWaterBlue, pillContent);
            else if (ring) DrawRing(D2D1::Point2F(ringX, cy), pillContent);
            if (g_set.clock) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                wchar_t t[32] = L"";
                GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, t, 32);
                float l = small && ring ? ringX + kRingR + 2 : x, rr = small && ring ? x + w - 6 : x + w;
                Text(t, g_fmtPillTime, D2D1::RectF(l, 0, rr, h), WithAlpha(g_fg, pillContent));
            }
        }
    }

    // ---- expanded content ----
    if (content > 0) {
        float px = (kWinW - kExpW) / 2;
        D2D1_COLOR_F white = WithAlpha(g_fg, content);

        if (g_toast && g_act) DrawActToast(content);
        else if (g_toast) {
            float tx = px + 26;
            if (g_toastGlyph) {
                g_brush->SetColor(WithAlpha(g_chip, content));
                g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + 23, 53), 23, 23), g_brush);
                Text(g_toastGlyph, g_fmtIcon, D2D1::RectF(tx, 30, tx + 46, 76), white);
                tx += 60;
            }
            if (g_toastLevel >= 0) {
                // title, level bar, then the subtitle (e.g. device name)
                wchar_t pct[16];
                swprintf(pct, 16, L"%d%%", (int)lroundf(g_toastLevel * 100));
                float right = px + 310;
                Text(g_toastMain, g_fmtTitle, D2D1::RectF(tx, 26, right - 50, 48), white);
                g_fmtPill->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                Text(pct, g_fmtPill, D2D1::RectF(right - 60, 30, right, 48), WithAlpha(g_grey, content));
                g_fmtPill->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                D2D1_RECT_F track = D2D1::RectF(tx, 54, right, 60);
                g_brush->SetColor(WithAlpha(g_fg, 0.18f * content));
                g_rt->FillRoundedRectangle(D2D1::RoundedRect(track, 3, 3), g_brush);
                if (g_toastLevel > 0) {
                    D2D1_RECT_F fill = track;
                    fill.right = track.left + (track.right - track.left) * Clamp01(g_toastLevel);
                    g_brush->SetColor(g_toastLevelWarn ? WithAlpha(kRed, content) : g_toastLevelDim ? WithAlpha(g_grey, content) : white);
                    g_rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 3, 3), g_brush);
                }
                Text(g_toastSub, g_fmtMeta, D2D1::RectF(tx, 66, right, 84), WithAlpha(g_grey, content));
            } else {
                // Big clock-size text when it fits; longer text steps down.
                float room = px + 310 - tx;
                if (TextWidth(g_toastMain, g_fmtTime) <= room) {
                    Text(g_toastMain, g_fmtTime, D2D1::RectF(tx, 18, px + 310, 70), white);
                    Text(g_toastSub, g_fmtDate, D2D1::RectF(tx + 2, 68, px + 310, 92), WithAlpha(g_grey, content));
                } else {
                    IDWriteTextFormat* f = TextWidth(g_toastMain, g_fmtToast) <= room ? g_fmtToast : g_fmtTitle; // g_fmtTitle trims with "â€¦"
                    Text(g_toastMain, f, D2D1::RectF(tx, 30, px + 310, 60), white);
                    Text(g_toastSub, g_fmtMeta, D2D1::RectF(tx + 1, 62, px + 310, 80), WithAlpha(g_grey, content));
                }
            }
        } else {
            if (g_ask) DrawAsk(content);
            else {
            // ---- pages: header status, the page (sliding when it changes), arrows and dots ----
            DrawHeaderStatus(content);
            g_rt->PushAxisAlignedClip(D2D1::RectF(px + 1, 0, px + kExpW - 1, h), D2D1_ANTIALIAS_MODE_ALIASED);
            float t = g_pageT;
            if (t < 1) DrawPage(g_pagePrev, content * (1 - t), -g_pageDir * 48 * t);
            DrawPage(g_page, content * t, g_pageDir * 48 * (1 - t));
            g_rt->PopAxisAlignedClip();
            DrawPageNav(content);
            }
        }
    }

    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    SafeRelease(shape);
    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) { ReleaseSurface(); return; }

    POINT src = {0, 0};
    SIZE size = {g_pxW, g_pxH};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (g_fullUpdate || dpx.right <= dpx.left || dpx.bottom <= dpx.top) {
        UpdateLayeredWindow(g_hwnd, nullptr, nullptr, &size, g_memDC, &src, 0, &blend, ULW_ALPHA);
        g_fullUpdate = false;
    } else {
        UPDATELAYEREDWINDOWINFO info = {sizeof(info), nullptr, nullptr, &size, g_memDC, &src, 0, &blend, ULW_ALPHA, &dpx};
        if (!UpdateLayeredWindowIndirect(g_hwnd, &info)) UpdateLayeredWindow(g_hwnd, nullptr, nullptr, &size, g_memDC, &src, 0, &blend, ULW_ALPHA);
    }
}

// =====================================================================
// Behaviour
// =====================================================================
static void KickAnim() {
    if (!g_animating) { g_animating = true; g_animLast.QuadPart = 0; SetTimer(g_hwnd, TIMER_ANIM, 16, nullptr); } // ~60 frames a second; the springs run on real time
}

static void GlassCapture();
static void SetTarget(float t) {
    if (t > 0.5f && g_target < 0.5f && g_t < 0.05f) GlassCapture(); // opening: a fresh copy of what's behind
    g_target = t;
    KickAnim();
}

static bool TodoHoldOpen();
static bool AskHoldOpen();
static void UpdateTarget() { SetTarget((g_hoverOpen || g_flash || g_dragOver || TodoHoldOpen() || AskHoldOpen()) ? 1.0f : 0.0f); }

// Switches the open notch to `page`, sliding the old one out (dir +1: from the right, -1: from the left).
static int PageSlot(int kind) { for (int i = 0; i < g_pageN; i++) if (g_pages[i] == kind) return i; return -1; }
static int PageStep(int delta) { // the page delta steps away in the shown order, wrapping around
    int i = PageSlot(g_page);
    if (i < 0) i = 0;
    return g_pages[((i + delta) % g_pageN + g_pageN) % g_pageN];
}
static void RebuildPages() { // [Pages] changed
    g_pageN = Settings_PageOrder(g_set, g_pages);
    if (PageSlot(g_page) < 0) { g_page = g_pagePrev = g_pages[0]; g_pageT = 1; }
}
static void GoPage(int page, int dir) {
    if (page < 0 || page >= PAGE_COUNT) return;
    if (page == g_page) return;
    g_pagePrev = g_page;
    g_page = page;
    g_pageDir = dir;
    g_pageT = 0;
    g_hot = HOT_NONE;
    KickAnim();
}

static void ShowToast(const wchar_t* main, const wchar_t* sub, const wchar_t* glyph, float level, bool levelDim) {
    if (g_act) return; // a task reminder stays until it is answered
    wcsncpy(g_toastMain, main, 63);
    g_toastMain[63] = 0;
    wcsncpy(g_toastSub, sub, 95);
    g_toastSub[95] = 0;
    g_toastGlyph = glyph;
    g_toastLevel = level;
    g_toastLevelDim = levelDim;
    g_toastLevelWarn = false;
    g_toast = true;
    KickAnim(); // the shelf strip tucks away under pop-ups

    // Publish "main | sub" as a window property so tests can read what was shown.
    wchar_t text[200];
    swprintf(text, 200, L"%ls | %ls", g_toastMain, g_toastSub);
    text[199] = 0;
    if (g_toastAtom) GlobalDeleteAtom(g_toastAtom);
    g_toastAtom = GlobalAddAtomW(text);
    SetPropW(g_hwnd, L"PerchToastAtom", (HANDLE)(ULONG_PTR)g_toastAtom);
    g_flash = true;
    SetTimer(g_hwnd, TIMER_FLASH, 1600, nullptr);
    UpdateTarget();
    Render();
}

// One step of a damped spring (omega rad/s, zeta damping), in small sub-steps so it stays stable.
static void SpringStep(float& x, float& v, float target, float omega, float zeta, float dt) {
    for (float left = dt; left > 0; left -= 0.004f) {
        float h = std::min(left, 0.004f);
        v += (-omega * omega * (x - target) - 2 * zeta * omega * v) * h;
        x += v * h;
    }
}
static bool Settled(float x, float v, float target, float eps) { return std::fabs(x - target) < eps && std::fabs(v) < eps * 10; }

static void OnAnimTick() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = g_animLast.QuadPart ? (float)(now.QuadPart - g_animLast.QuadPart) / freq.QuadPart : 0.016f;
    dt = std::min(std::max(dt, 0.001f), 0.05f);
    g_animLast = now;
    if (g_t < 0.5f) g_iconBarTarget = 0; // the page icons only show while open
    if (g_cw <= 0) { g_cw = CollapsedWidth(); g_ch = CollapsedHeight(); }
    float speed = g_set.fastAnim ? 1.4f : 1.0f; // Animation speed Normal / Fast
    SpringStep(g_t, g_tv, g_target, (g_target > 0.5f ? 17.0f : 19.5f) * speed, g_target > 0.5f ? 0.72f : 0.8f, dt); // ~3% over opening, ~1.5% under closing
    SpringStep(g_cw, g_cwv, CollapsedWidth(), 19.5f, 0.77f, dt);  // pill <-> Minimal tab
    SpringStep(g_ch, g_chv, CollapsedHeight(), 19.5f, 0.77f, dt);
    SpringStep(g_peek, g_peekv, g_peekTarget, 20, 1, dt);         // hover swell
    g_iconBar += (g_iconBarTarget - g_iconBar) * (1 - expf(-dt / 0.042f));
    g_pageT += (1 - g_pageT) * (1 - expf(-dt / 0.06f));
    if (Settled(g_t, g_tv, g_target, 0.002f) && std::fabs(g_iconBarTarget - g_iconBar) < 0.002f && g_pageT > 0.998f &&
        Settled(g_cw, g_cwv, CollapsedWidth(), 0.05f) && Settled(g_ch, g_chv, CollapsedHeight(), 0.05f) && Settled(g_peek, g_peekv, g_peekTarget, 0.002f)) {
        g_t = g_target;
        g_tv = g_cwv = g_chv = g_peekv = 0;
        g_cw = CollapsedWidth();
        g_ch = CollapsedHeight();
        g_peek = g_peekTarget;
        g_animLast.QuadPart = 0;
        g_iconBar = g_iconBarTarget;
        g_pageT = 1;
        g_animating = false;
        KillTimer(g_hwnd, TIMER_ANIM);
        if (g_t >= 1.0f) SetTimer(g_hwnd, TIMER_CLOCK, 1000, nullptr);
        else KillTimer(g_hwnd, TIMER_CLOCK);
        if (g_t == 0.0f) { g_toast = false; PetRecheck(); GlassCapture(); }
    }
    Render();
    // Once fully collapsed, hand idle pages back to Windows.
    if (!g_animating && g_t == 0.0f) SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// ---- shelf interaction ----
// Opens the notch for a moment so you can see what just landed on the shelf.
static void ShowShelfBriefly() {
    g_toast = false;
    GoPage(PAGE_SHELF, 1);
    g_pageT = 1;
    g_flash = true;
    SetTimer(g_hwnd, TIMER_FLASH, 1800, nullptr);
    UpdateTarget();
    KickAnim();
}

static bool HasFiles(IDataObject* d) {
    FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return d && d->QueryGetData(&fe) == S_OK;
}

static int AddFilesFromData(IDataObject* d) {
    FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm = {};
    if (FAILED(d->GetData(&fe, &sm))) return 0;
    int added = 0;
    if (HDROP drop = (HDROP)GlobalLock(sm.hGlobal)) {
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            wchar_t path[MAX_PATH * 4];
            if (DragQueryFileW(drop, i, path, MAX_PATH * 4) && ShelfAdd(path)) added++;
        }
        GlobalUnlock(sm.hGlobal);
    }
    ReleaseStgMedium(&sm);
    return added;
}

// Files dragged from Explorer (or any app) onto the notch.
class DropTarget final : public IDropTarget {
    LONG m_ref = 1;
    bool m_accept = false;

public:
    STDMETHODIMP QueryInterface(REFIID iid, void** pp) override {
        if (IsEqualIID(iid, kIID_IUnknown) || IsEqualIID(iid, kIID_IDropTarget)) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP DragEnter(IDataObject* d, DWORD, POINTL, DWORD* effect) override {
        m_accept = !g_draggingOut && HasFiles(d); // not our own tiles coming back
        *effect = m_accept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (m_accept) { g_dragOver = true; g_shelfTab = 0; GoPage(PAGE_SHELF, 1); g_pageT = 1; UpdateTarget(); Render(); }
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = m_accept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        if (m_accept) { g_dragOver = false; UpdateTarget(); Render(); }
        m_accept = false;
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* d, DWORD, POINTL, DWORD* effect) override {
        *effect = DROPEFFECT_NONE;
        if (m_accept) {
            g_dragOver = false;
            if (AddFilesFromData(d) > 0) { *effect = DROPEFFECT_COPY; ShelfChanged(); }
            ShowShelfBriefly();
        }
        m_accept = false;
        return S_OK;
    }
};
static DropTarget* g_dropTarget;

// Drags shelf items into another app. Items that land somewhere leave the shelf.
static void ShelfDragOut(std::vector<int> idx) {
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (int i : idx) {
        PIDLIST_ABSOLUTE p = nullptr;
        if (SUCCEEDED(SHParseDisplayName(g_shelf[i].path.c_str(), nullptr, &p, 0, nullptr))) pidls.push_back(p);
    }
    IShellItemArray* arr = nullptr;
    IDataObject* data = nullptr;
    if (!pidls.empty() && SUCCEEDED(SHCreateShellItemArrayFromIDLists((UINT)pidls.size(), (PCIDLIST_ABSOLUTE_ARRAY)pidls.data(), &arr))) {
        arr->BindToHandler(nullptr, kBHID_DataObject, kIID_IDataObject, (void**)&data);
        arr->Release();
    }
    for (PIDLIST_ABSOLUTE p : pidls) CoTaskMemFree(p);
    if (!data) return;

    g_draggingOut = true;
    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr = SHDoDragDrop(g_hwnd, data, nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    g_draggingOut = false;
    data->Release();
    if (hr == DRAGDROP_S_DROP) {
        std::sort(idx.rbegin(), idx.rend());
        for (int i : idx) ShelfRemove(i);
        ShelfChanged();
    }
}

static void ShelfClear() {
    while (!g_shelf.empty()) ShelfRemove((int)g_shelf.size() - 1);
    ShelfChanged();
}

static void ShowTileMenu(int item, POINT screenPt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_TILE_OPEN, L"Open");
    AppendMenuW(m, MF_STRING, CMD_TILE_SHOW, L"Show in folder");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_TILE_REMOVE, L"Remove from shelf");
    g_menuTile = item;
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

// "Perch.exe --shelf a.txt b.png" sends files to the running notch (one per line).
static void AddShelfFromText(const wchar_t* text, size_t len) {
    std::wstring s(text, len);
    size_t start = 0;
    int added = 0;
    while (start < s.size()) {
        size_t end = s.find(L'\n', start);
        if (end == std::wstring::npos) end = s.size();
        if (ShelfAdd(s.substr(start, end - start))) added++;
        start = end + 1;
    }
    if (added) ShelfChanged();
    ShowShelfBriefly();
}

// ---- settings (1.5) ----
static std::wstring SettingsFile() { return DataDir() + L"\\settings.ini"; }

static FILETIME SettingsStamp() { // last-write time, or zero when there's no file
    WIN32_FILE_ATTRIBUTE_DATA fa;
    FILETIME none = {0, 0};
    return GetFileAttributesExW(SettingsFile().c_str(), GetFileExInfoStandard, &fa) ? fa.ftLastWriteTime : none;
}

// ---- calendar download (a worker thread; the result comes back as WM_APP_CAL_DONE) ----
static std::wstring CalCacheFile() { return DataDir() + L"\\calendar.ics"; } // last download, so it shows offline
static DWORD WINAPI CalThread(void* p) {
    CalJob* j = (CalJob*)p;
    j->code = Cal_Download(j->link, j->body);
    if (!PostMessageW(g_hwnd, WM_APP_CAL_DONE, 0, (LPARAM)j)) delete j;
    return 0;
}
static void CalFetch(bool announce) {
    KillTimer(g_hwnd, TIMER_CAL);
    if (g_set.calLink.empty() || g_calBusy) return;
    g_calBusy = true;
    g_calAnnounce = announce;
    if (g_calStatus != CAL_CONNECTED) g_calStatus = CAL_CHECKING;
    HANDLE t = CreateThread(nullptr, 64 * 1024, CalThread, new CalJob{g_set.calLink}, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) CloseHandle(t);
    else g_calBusy = false;
}
static bool CalLoad(const std::string& ics) {
    std::vector<CalEvent> ev;
    int today = Cal_Today();
    if (!Cal_Parse(ics, today - 400, today + 400, ev)) return false;
    g_calEvents.swap(ev);
    ArmCalRemTimer();
    return true;
}
static void CalLinkChanged(bool announce) { // settings loaded: a new (or no) link starts over
    if (g_set.calLink == g_calLinkInUse && !g_calFirst) return;
    bool first = g_calFirst;
    g_calFirst = false;
    g_calLinkInUse = g_set.calLink;
    g_calEvents.clear();
    g_calStatus = g_set.calLink.empty() ? CAL_NONE : CAL_CHECKING;
    KillTimer(g_hwnd, TIMER_CAL);
    if (g_set.calLink.empty()) { DeleteFileW(CalCacheFile().c_str()); return; }
    if (first) { // start-up: show the last download straight away, then fetch a fresh one
        HANDLE f = CreateFileW(CalCacheFile().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD size = GetFileSize(f, nullptr), got = 0;
            std::string ics(size < (32u << 20) ? size : 0, '\0');
            if (!ics.empty() && ReadFile(f, &ics[0], size, &got, nullptr) && CalLoad(ics)) {
                g_calStatus = CAL_CONNECTED;
                FILETIME ft, lt;
                SYSTEMTIME st;
                if (GetFileTime(f, nullptr, nullptr, &ft) && FileTimeToLocalFileTime(&ft, &lt) && FileTimeToSystemTime(&lt, &st))
                    swprintf(g_calUpdatedAt, 16, L"%02d:%02d", st.wHour, st.wMinute);
            }
            CloseHandle(f);
        }
    } else DeleteFileW(CalCacheFile().c_str());
    CalFetch(announce);
}
static bool g_calAnnounceNext = false; // the next link change came from "Paste calendar link"

// Reads settings.ini and applies it. `announce` confirms a live reload from the file with a toast;
// `quiet` (changes made in the settings window) shows nothing, since the window shows problems itself.
static void ApplySettings(bool announce, bool quiet = false) {
    g_setStamp = SettingsStamp();
    g_setLoads++;
    int oldPet = g_set.mascotStyle;
    std::wstring problem = Settings_Load(SettingsFile(), g_set);
    if (fabsf(WantedScale() - g_scale) > 0.001f) { // Notch size changed
        g_scale = WantedScale();
        CreateSurface();
    }
    ArmMinuteTimer();
    if (g_hwnd) { ArmWaterTimer(); ClipListen(); ArmCalRemTimer(); AskHotkey(); }
    UpdateKeyboardHook();
    SetGlassColours(g_set.glass);
    if (g_set.glass != g_glassOn) SetGlass(g_set.glass);
    PlaceWindow(); // Classic / Floating
    SettingsUI_Refresh(g_set);
    RebuildPages();
    if (!g_set.hideFullscreen && g_fsHidden) SetFullscreenHidden(false);
    PetRecheck();
    KickAnim(); // a new size or Minimal springs into place
    CalLinkChanged(g_calAnnounceNext);
    g_calAnnounceNext = false;
    if (!quiet && !problem.empty()) ShowToast(L"Settings problem", problem.c_str(), L"\xE7BA"); // Warning
    else if (!quiet && announce) ShowToast(L"Settings updated", L"settings.ini", L"\xE713");  // Settings
    else if (!g_hidden) Render();
    if (g_set.mascotStyle != oldPet && g_petMode == PET_OFF && PetCanAppear()) PetStart(); // a new style (no pop-up showing): show it
}

static void OpenSettingsFile() {
    std::wstring file = SettingsFile();
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Settings_CreateDefault(file);
        g_setStamp = SettingsStamp(); // same values as the defaults in use: no reload needed
    }
    std::wstring arg = L"\"" + file + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

// ---- settings window ----
static HINSTANCE g_inst;
static void UiCommit(const Settings& s) {
    Settings_Save(SettingsFile(), s);
    ApplySettings(false, true);
}
static void CalPasteLink();
static void CalSetLink(const std::wstring& link);
static void CalFetch(bool announce);
static void UiCalStatus(wchar_t* out, int n) { // the status under "Calendar link"
    int ev = 0, today = Cal_Today();
    for (const CalEvent& e : g_calEvents) ev += e.day >= today && e.day < today + 30;
    switch (g_calStatus) {
    case CAL_CHECKING: swprintf(out, n, L"Checking your calendar"); break;
    case CAL_CONNECTED:
        if (ev == 1) swprintf(out, n, L"Connected \x00B7 1 event \x00B7 Updated %ls", g_calUpdatedAt);
        else swprintf(out, n, L"Connected \x00B7 %d events \x00B7 Updated %ls", ev, g_calUpdatedAt);
        break;
    case CAL_UNREACHABLE: swprintf(out, n, L"Couldn't reach your calendar. Perch will try again."); break;
    case CAL_NOT_ICAL: swprintf(out, n, L"That link isn't an iCal calendar"); break;
    default: swprintf(out, n, L"Not connected");
    }
}
static void OpenSettings(int section = -1) {
    SettingsUIHost host = {UiCommit, OpenSettingsFile, AutostartEnabled, SetAutostart,
                           CalPasteLink, []() { CalFetch(true); }, []() { CalSetLink(L""); }, UiCalStatus, UiWaterStatus,
                           AskPasteKey, AskRemoveKey, UiAskStatus, UiAskModels, UiAskHotkeyOk};
    SettingsUI_Show(g_inst, g_set, host, section);
}

// A download finished: a message for each outcome when it was asked for (paste, Refresh).
static void OnCalDone(CalJob* j) {
    g_calBusy = false;
    bool announce = g_calAnnounce;
    if (j->link != g_set.calLink) { delete j; CalFetch(false); return; } // the link changed meanwhile
    wchar_t sub[96];
    if (j->code != 0) {
        if (g_calStatus != CAL_CONNECTED) g_calStatus = CAL_UNREACHABLE;
        SetTimer(g_hwnd, TIMER_CAL, 5 * 60000, nullptr); // try again
        if (announce) ShowToast(L"Couldn't reach your calendar", L"Perch will try again.", L"\xE787");
    } else if (!CalLoad(j->body)) {
        g_calStatus = CAL_NOT_ICAL;
        g_calEvents.clear();
        if (announce) ShowToast(L"That link isn't an iCal calendar", L"Copy the \"Secret address in iCal format\"", L"\xE787");
    } else {
        g_calStatus = CAL_CONNECTED;
        SYSTEMTIME now;
        GetLocalTime(&now);
        swprintf(g_calUpdatedAt, 16, L"%02d:%02d", now.wHour, now.wMinute);
        HANDLE f = CreateFileW(CalCacheFile().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, j->body.data(), (DWORD)j->body.size(), &w, nullptr); CloseHandle(f); }
        SetTimer(g_hwnd, TIMER_CAL, 30 * 60000, nullptr);
        if (announce) {
            int today = Cal_Today(), ahead = 0;
            for (const CalEvent& e : g_calEvents) ahead += e.day >= today && e.day < today + 30;
            swprintf(sub, 96, ahead == 1 ? L"1 event in the next 30 days" : L"%d events in the next 30 days", ahead);
            ShowToast(L"Calendar connected", sub, L"\xE787");
        }
    }
    delete j;
    SettingsUI_Refresh(g_set); // the To-do section shows the calendar's status
    if (!g_hidden) Render();
}

static void CalPasteLink() { // "Paste link"
    std::wstring text;
    if (OpenClipboard(g_hwnd)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { text = p; GlobalUnlock(h); }
        }
        CloseClipboard();
    }
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    while (!text.empty() && iswspace(text.front())) text.erase(0, 1);
    bool link = _wcsnicmp(text.c_str(), L"https://", 8) == 0 || _wcsnicmp(text.c_str(), L"http://", 7) == 0 ||
                _wcsnicmp(text.c_str(), L"webcal://", 9) == 0;
    if (!link || text.find_first_of(L" \t\r\n") != std::wstring::npos) {
        ShowToast(L"No link on the clipboard", L"Copy the \"Secret address in iCal format\", then paste again.", L"\xE787");
        return;
    }
    Settings s = g_set;
    s.calLink = text;
    g_calAnnounceNext = true;
    ShowToast(L"Checking your calendar", L"Google Calendar", L"\xE787");
    Settings_Save(SettingsFile(), s);
    ApplySettings(false, true);
}
static void CalSetLink(const std::wstring& link) {
    Settings s = g_set;
    s.calLink = link;
    Settings_Save(SettingsFile(), s);
    ApplySettings(false, true);
}

static int GetStateFlags() {
    int f = 0;
    if (g_t >= 1.0f) f |= ST_EXPANDED;
    if (g_fsHidden) f |= ST_FULLSCREEN_HIDDEN;
    if (g_hidden) f |= ST_HIDDEN;
    if (g_hover) f |= ST_HOVER;
    if (AutostartEnabled()) f |= ST_AUTOSTART;
    if (g_toast) f |= ST_TOAST;
    if (g_media.hasSession) f |= ST_HAS_MEDIA;
    if (g_media.playing) f |= ST_PLAYING;
    if (!g_media.art.empty()) f |= ST_ART;
    return f;
}

static void OnMediaChanged() {
    Media_Get(g_media);
    // Window title = what's playing (handy for screen readers and for tests).
    wchar_t title[600] = L"Perch";
    if (g_media.hasSession) swprintf(title, 600, L"Perch - %ls - %ls", g_media.title, g_media.artist);
    SetWindowTextW(g_hwnd, title);
    PetRecheck();
    if (std::fabs(g_cw - CollapsedWidth()) > 0.05f || std::fabs(g_ch - CollapsedHeight()) > 0.05f) KickAnim();
    if (!g_hidden) Render();
    // Loading media/artwork pulls in WinRT + WIC; hand those pages back while collapsed.
    if (g_t == 0.0f && !g_animating) SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// ---- tray icon (an icon by the clock; its menu has Settings, Start with Windows, the mascot,
// Minimal notch, Notch size, About and Exit) ----
static NOTIFYICONDATAW g_nid;
static UINT g_msgTaskbarCreated;
static void AddTrayIcon() {
    g_nid = NOTIFYICONDATAW{};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy(g_nid.szTip, L"Perch");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}
static void RemoveTrayIcon() {
    if (!g_nid.hWnd) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
    g_nid.hWnd = nullptr;
}
static void ShowTrayMenu(POINT pt) {
    HMENU m = CreatePopupMenu(), size = CreatePopupMenu();
    static const wchar_t* sizes[5] = {L"Auto", L"100%", L"115%", L"130%", L"150%"};
    for (int i = 0; i < 5; i++) AppendMenuW(size, MF_STRING | (g_set.size == i ? MF_CHECKED : 0), CMD_SIZE0 + i, sizes[i]);
    AppendMenuW(m, MF_STRING, CMD_SETTINGS, L"Settings\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), CMD_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_STRING | (g_set.mascot ? MF_CHECKED : 0), CMD_MASCOT, L"Perch mascot");
    AppendMenuW(m, MF_STRING | (g_set.minimal ? MF_CHECKED : 0), CMD_MINIMAL, L"Minimal notch");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)size, L"Notch size");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_ABOUT, L"About Perch");
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Exit");
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m); // destroys the submenu too
    if (cmd) PostMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static void HandleCommand(int cmd) {
    switch (cmd) {
    case CMD_QUIT: DestroyWindow(g_hwnd); break;
    case CMD_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
    case CMD_MEDIA_PLAYPAUSE: Media_Command(MEDIA_PLAYPAUSE); break;
    case CMD_MEDIA_NEXT: Media_Command(MEDIA_NEXT); break;
    case CMD_MEDIA_PREV: Media_Command(MEDIA_PREV); break;
    case CMD_SHELF_CLEAR: ShelfClear(); break;
    case CMD_SETTINGS: OpenSettings(); break;
    case CMD_POMO_TOGGLE: PomoToggle(); if (!g_hidden) Render(); break;
    case CMD_POMO_RESET: PomoReset(); if (!g_hidden) Render(); break;
    case CMD_CAL_PASTE: CalPasteLink(); break;
    case CMD_MINIMAL: { Settings s = g_set; s.minimal = !s.minimal; Settings_Save(SettingsFile(), s); ApplySettings(false, true); break; }
    case CMD_MASCOT: { Settings s = g_set; s.mascot = !s.mascot; Settings_Save(SettingsFile(), s); ApplySettings(false, true); break; }
    case CMD_ABOUT: OpenSettings(SEC_ABOUT); break;
    case CMD_ASK: AskOpen(); break;
    case CMD_ASK_KEY: AskPasteKey(); break;
    case CMD_CAL_REFRESH: CalFetch(true); break;
    case CMD_CAL_REMOVE: CalSetLink(L""); break;
    case CMD_SIZE0: case CMD_SIZE0 + 1: case CMD_SIZE0 + 2: case CMD_SIZE0 + 3: case CMD_SIZE0 + 4: {
        Settings s = g_set;
        s.size = cmd - CMD_SIZE0;
        Settings_Save(SettingsFile(), s);
        ApplySettings(false, true);
        break;
    }
    case CMD_TILE_OPEN:
        if (g_menuTile >= 0 && g_menuTile < (int)g_shelf.size())
            ShellExecuteW(nullptr, L"open", g_shelf[g_menuTile].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_TILE_SHOW:
        if (g_menuTile >= 0 && g_menuTile < (int)g_shelf.size()) {
            std::wstring args = L"/select,\"" + g_shelf[g_menuTile].path + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case CMD_TILE_REMOVE:
        if (g_menuTile >= 0 && g_menuTile < (int)g_shelf.size()) { ShelfRemove(g_menuTile); ShelfChanged(); }
        break;
    }
}

static void ShowMenu(POINT screenPt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Perch");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), CMD_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_STRING | (g_shelf.empty() ? MF_GRAYED : 0), CMD_SHELF_CLEAR, L"Clear shelf");
    AppendMenuW(m, MF_STRING | (g_set.minimal ? MF_CHECKED : 0), CMD_MINIMAL, L"Minimal notch"); // a small tab while nothing plays
    AppendMenuW(m, MF_STRING, CMD_ASK, (L"Ask Perch" + (g_askHotkeyOk && _wcsicmp(g_set.askHotkey.c_str(), L"Off") ? L"\t" + g_set.askHotkey : std::wstring())).c_str());
    AppendMenuW(m, MF_STRING, CMD_CAL_PASTE, g_set.calLink.empty() ? L"Paste calendar link" : L"Paste new calendar link");
    if (!g_set.calLink.empty()) {
        AppendMenuW(m, MF_STRING, CMD_CAL_REFRESH, L"Refresh calendar");
        AppendMenuW(m, MF_STRING, CMD_CAL_REMOVE, L"Remove calendar link");
    }
    AppendMenuW(m, MF_STRING, CMD_SETTINGS, L"Settings\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit");
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) HandleCommand(cmd);
}

// Clickable spot of the open notch under (x, y) DIPs, or HOT_NONE.
static bool PagesActive() { return g_t >= 0.9f && !g_toast; }
static int HitHot(float x, float y) {
    if (g_toast && g_act) return g_t >= 0.9f ? TodoHit(x, y) : HOT_NONE;
    if (g_ask) {
        if (g_t < 0.9f || g_toast) return HOT_NONE;
        if (InRect(AskCopyRect(), x, y)) return !AskHasKey() ? HOT_ASK_SETTINGS : !g_askAnswer.empty() && !g_askBusy ? HOT_ASK_COPY : HOT_NONE;
        return HOT_NONE;
    }
    if (!PagesActive()) return HOT_NONE;
    auto over = [&](int hot) {
        D2D1_POINT_2F c = HotCenter(hot);
        float r = HotRadius(hot) + 2;
        return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= r * r;
    };
    if (over(HOT_ARROW_L)) return HOT_ARROW_L;
    if (over(HOT_ARROW_R)) return HOT_ARROW_R;
    if (g_iconBar > 0.5f) {
        for (int i = 0; i < g_pageN; i++) if (over(HOT_ICON0 + i)) return HOT_ICON0 + i;
        if (over(HOT_ICON_SETTINGS)) return HOT_ICON_SETTINGS;
    }
    if (g_pageT < 0.99f) return HOT_NONE;
    if (g_page == PAGE_MUSIC)
        for (int hot : {HOT_LYRICS, HOT_PREV, HOT_PLAY, HOT_NEXT, HOT_VOLUME}) if (over(hot)) return hot;
    if (g_page == PAGE_TODO) return TodoHit(x, y);
    if (g_page == PAGE_POMODORO) {
        float px = PX();
        if (x >= px + 280 && x < px + 340 && y >= 53 && y < 75) return HOT_POMO_START;
        if (over(HOT_POMO_RESET)) return HOT_POMO_RESET;
    }
    if (g_page == PAGE_WATER) {
        D2D1_RECT_F r = WaterAddRect();
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return HOT_WATER_ADD;
        if (over(HOT_WATER_UNDO)) return HOT_WATER_UNDO;
    }
    if (g_page == PAGE_SHELF && g_set.clipHistory && !g_toast) {
        for (int t = 0; t < 2; t++) if (InRect(ShelfTabRect(t), x, y)) return HOT_SHELF_TAB0 + t;
        if (ClipActive()) {
            if (!g_clip.empty() && InRect(ClipClearRect(), x, y)) return HOT_CLIP_CLEAR;
            for (int row = 0; row < kClipRows && g_clipScroll + row < (int)g_clip.size(); row++) {
                if (over(HOT_CLIP_DEL0 + row)) return HOT_CLIP_DEL0 + row;
                if (InRect(ClipRowRect(row), x, y)) return HOT_CLIP_ROW0 + row;
            }
            return HOT_NONE;
        }
    }
    if (ShelfInteractive()) {
        auto in = [&](D2D1_RECT_F r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; };
        if (in(ShelfClearRect())) return HOT_SHELF_CLEAR;
    }
    return HOT_NONE;
}

static ULONGLONG g_selfMuteAt; // when our own speaker button changed the mute (its pop-up is skipped)
static void ToggleOutputMute() {
    if (!g_outVol) return;
    BOOL m = FALSE;
    g_outVol->GetMute(&m);
    g_selfMuteAt = GetTickCount64();
    g_outVol->SetMute(!m, nullptr);
    Render(); // the button shows the new state; no volume pop-up over the page
}

static void ClickHot(int hot) {
    switch (hot) {
    case HOT_ARROW_L: GoPage(PageStep(-1), -1); break;
    case HOT_ARROW_R: GoPage(PageStep(1), 1); break;
    case HOT_PREV: HandleCommand(CMD_MEDIA_PREV); break;
    case HOT_PLAY: HandleCommand(CMD_MEDIA_PLAYPAUSE); break;
    case HOT_NEXT: HandleCommand(CMD_MEDIA_NEXT); break;
    case HOT_VOLUME: ToggleOutputMute(); break;
    case HOT_SHELF_CLEAR: ShelfClear(); break;
    case HOT_SHELF_TAB0: case HOT_SHELF_TAB0 + 1: g_shelfTab = hot - HOT_SHELF_TAB0; g_clipScroll = 0; break;
    case HOT_CLIP_CLEAR: ClipClear(); break;
    case HOT_ASK_COPY: AskCopyAnswer(); break;
    case HOT_ASK_SETTINGS: AskClose(); OpenSettings(SEC_ASK); break;
    case HOT_POMO_START: PomoToggle(); break;
    case HOT_POMO_RESET: PomoReset(); break;
    case HOT_WATER_ADD: WaterAdd(g_set.waterCup); break;
    case HOT_WATER_UNDO: WaterAdd(-g_set.waterCup); break;
    case HOT_TODO_ADD: case HOT_TODO_EDIT: case HOT_TODO_CAL: case HOT_TODO_TITLE: case HOT_TODO_TIME: case HOT_TODO_DAILY:
    case HOT_TODO_CANCEL: case HOT_TODO_SAVE: case HOT_TODO_CLEAR: case HOT_ACT_A: case HOT_ACT_B:
    case HOT_CAL_PREV: case HOT_CAL_NEXT: case HOT_CAL_TITLE: TodoClick(hot); break;
    case HOT_ICON_SETTINGS: OpenSettings(); break;
    default:
        if (hot >= HOT_CLIP_ROW0 && hot < HOT_CLIP_ROW0 + kClipRows) ClipCopy(g_clipScroll + hot - HOT_CLIP_ROW0);
        else if (hot >= HOT_CLIP_DEL0 && hot < HOT_CLIP_DEL0 + kClipRows) ClipRemove(g_clipScroll + hot - HOT_CLIP_DEL0);
        else if (hot >= HOT_ICON0 && hot < HOT_ICON0 + g_pageN) GoPage(g_pages[hot - HOT_ICON0], hot - HOT_ICON0 > PageSlot(g_page) ? 1 : -1);
        else if ((hot >= HOT_TODO_ROW0 && hot < HOT_TODO_MAX) || (hot >= HOT_CAL_DAY0 && hot < HOT_CAL_DAYMAX)) TodoClick(hot);
    }
    Render();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!g_hover) {
            g_hover = true;
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            if (g_t > 0.5f || g_flash) { // already open (a pop-up): stay open
                if (!g_flash) g_toast = false;
                g_hoverOpen = true;
                UpdateTarget();
            } else { // the pill swells a little, then opens
                g_peekTarget = 1;
                KickAnim();
                if (!g_set.openClick) SetTimer(hwnd, TIMER_PEEK, g_set.fastAnim ? 150 : 230, nullptr); // Click: it only peeks until clicked
            }
        }
        { // remove button lights up red under the pointer; controls get a hover circle
            float mx = GET_X_LPARAM(lp) / g_scale, my = GET_Y_LPARAM(lp) / g_scale - g_offY;
            int hc = ShelfHitClose(mx, my), hot = HitHot(mx, my);
            D2D1_RECT_F dr = DotsRect();
            float bar = PagesActive() && mx >= dr.left && mx < dr.right && my >= dr.top && my < dr.bottom ? 1.0f : 0.0f;
            if (!g_set.pageIcons) bar = 0; // "Page icons" off
            if (bar != g_iconBarTarget) { g_iconBarTarget = bar; KickAnim(); }
            if (hc != g_hoverClose || hot != g_hot) { g_hoverClose = hc; g_hot = hot; Render(); }
        }
        // Dragging a shelf tile out: start once the mouse moves past the drag threshold.
        if (g_pressTile >= 0 && (wp & MK_LBUTTON)) {
            int dx = std::abs(GET_X_LPARAM(lp) - g_pressPt.x), dy = std::abs(GET_Y_LPARAM(lp) - g_pressPt.y);
            if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG)) {
                int i = g_pressTile;
                g_pressTile = -1;
                ReleaseCapture();
                std::vector<int> idx;
                if (g_shelf[i].selected) {
                    for (int k = 0; k < (int)g_shelf.size(); k++)
                        if (g_shelf[k].selected) idx.push_back(k);
                } else {
                    idx.push_back(i);
                }
                ShelfDragOut(idx);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        g_hover = false;
        g_hoverOpen = false;
        g_peekTarget = 0;
        KillTimer(hwnd, TIMER_PEEK);
        g_hoverClose = -1;
        g_hot = HOT_NONE;
        g_iconBarTarget = 0;
        UpdateTarget();
        return 0;
    case WM_MOUSEWHEEL: { // the wheel flips pages (up = previous)
        static int acc = 0;
        if (g_ask) { g_askScroll = std::max(0.0f, g_askScroll - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 24); Render(); return 0; }
        if (!PagesActive()) return 0;
        if (g_page == PAGE_TODO && g_todoCal) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            float x = pt.x / g_scale, y = pt.y / g_scale - g_offY;
            int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
            if (InRect(TodoPanel(), x, y)) { TodoClick(d > 0 ? HOT_CAL_NEXT : HOT_CAL_PREV); Render(); return 0; }
            if (x >= PX() + 30 && x < PX() + 180 && y >= 106) { g_calScroll = std::max(0, g_calScroll + d); Render(); return 0; }
        }
        if (g_page == PAGE_TODO && !g_edit && !g_todoCal && (int)g_tasks.size() > kRowsShown) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (InRect(TodoPanel(), pt.x / g_scale, pt.y / g_scale - g_offY)) {
                int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
                g_todoScroll = std::max(0, std::min(g_todoScroll + d, (int)g_tasks.size() - kRowsShown));
                Render();
                return 0;
            }
        }
        if (g_page == PAGE_SHELF && ClipActive() && (int)g_clip.size() > kClipRows) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y / g_scale - g_offY >= 56) {
                int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
                g_clipScroll = std::max(0, std::min(g_clipScroll + d, (int)g_clip.size() - kClipRows));
                Render();
                return 0;
            }
        }
        acc += GET_WHEEL_DELTA_WPARAM(wp);
        while (acc >= WHEEL_DELTA) { acc -= WHEEL_DELTA; GoPage(PageStep(-1), -1); }
        while (acc <= -WHEEL_DELTA) { acc += WHEEL_DELTA; GoPage(PageStep(1), 1); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int c = ShelfHitClose(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale - g_offY);
        if (c >= 0) { g_pressClose = c; SetCapture(hwnd); return 0; } // removed on release, if still over it
        int i = ShelfHitTile(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale - g_offY);
        if (i >= 0) {
            g_pressTile = i;
            g_pressPt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        // A double-click on a remove button removes one item, never the one that slid into its place.
        if (ShelfHitClose(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale - g_offY) >= 0) return 0;
        int i = ShelfHitTile(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale - g_offY);
        if (i >= 0) ShellExecuteW(nullptr, L"open", g_shelf[i].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_pressClose >= 0) {
            int i = g_pressClose;
            g_pressClose = -1;
            ReleaseCapture();
            if (ShelfHitClose(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale - g_offY) == i) {
                ShelfRemove(i);
                g_hoverClose = -1;
                ShelfChanged();
            }
            return 0;
        }
        if (g_pressTile >= 0) { // a click (not a drag) on a tile toggles its selection
            int i = g_pressTile;
            g_pressTile = -1;
            ReleaseCapture();
            if (i < (int)g_shelf.size()) { g_shelf[i].selected = !g_shelf[i].selected; Render(); }
            return 0;
        }
        if (g_t < 0.5f && g_hover && g_set.openClick) { g_hoverOpen = true; g_peekTarget = 0; if (!g_flash) g_toast = false; UpdateTarget(); return 0; } // Open the notch on Click
        if (g_t < 0.9f) return 0;
        float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale - g_offY;
        int hot = HitHot(x, y);
        if (hot != HOT_NONE) ClickHot(hot);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int tile = ShelfHitTile(p.x / g_scale, p.y / g_scale - g_offY);
        ClientToScreen(hwnd, &p);
        if (tile >= 0) ShowTileMenu(tile, p);
        else ShowMenu(p);
        return 0;
    }
    case WM_COPYDATA: { // files from "Perch.exe --shelf ..."
        const COPYDATASTRUCT* cds = (const COPYDATASTRUCT*)lp;
        if (cds && cds->dwData == 1 && cds->lpData) AddShelfFromText((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
        return TRUE;
    }
    case WM_APP_SHELF_COUNT:
        return (LRESULT)g_shelf.size();
    case WM_APP_SHELF_CLOSE: {
        int slot = (int)wp;
        if (!ShelfInteractive() || slot < 0 || slot >= ShelfSlots() || ShelfOverflowSlot(slot)) return -1;
        D2D1_POINT_2F c = ShelfCloseCenter(slot);
        return MAKELONG((int)(c.x * g_scale), (int)((c.y + g_offY) * g_scale));
    }
    case WM_APP_SHELF_TILE: {
        int slot = (int)wp;
        if (!ShelfInteractive() || slot < 0 || slot >= ShelfSlots()) return -1;
        D2D1_RECT_F r = ShelfTileRect(slot);
        int cx = (int)((r.left + r.right) / 2 * g_scale), cy = (int)((r.top + 24 + g_offY) * g_scale);
        return MAKELONG(cx, cy);
    }
    case WM_COMMAND:
        HandleCommand(LOWORD(wp));
        return 0;
    case WM_CHAR:
        if (g_ask) { AskChar((wchar_t)wp); return 0; }
        if (g_edit) { TodoChar((wchar_t)wp); return 0; }
        break;
    case WM_KEYDOWN:
        if (AskKeyDown(wp)) return 0;
        if (TodoKey(wp)) return 0;
        break;
    case WM_ACTIVATE: // typing in the editor keeps the notch open; leaving it lets it close
        if (LOWORD(wp) == WA_INACTIVE && g_ask && !g_askBusy) g_ask = false; // switched away: Ask closes (its text stays)
        UpdateTarget();
        if (!g_hidden) Render();
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // never steal focus from the app you're using
    case WM_TIMER:
        if (wp == TIMER_ANIM) OnAnimTick();
        else if (wp == TIMER_CLOCK) { static int n = 0; if (g_glassOn && ++n % 2 == 0) GlassCapture(); if (!g_hidden) Render(); } // open: the clock, and Glass's copy every 2 s
        else if (wp == TIMER_TODO) { KillTimer(hwnd, TIMER_TODO); CheckTaskReminders(NowMinute()); if (!g_hidden) Render(); }
        else if (wp == TIMER_ASKCOPY) { KillTimer(hwnd, TIMER_ASKCOPY); g_askCopied = 0; if (!g_hidden) Render(); }
        else if (wp == TIMER_CLIP) { KillTimer(hwnd, TIMER_CLIP); g_clipCopied = -1; if (!g_hidden) Render(); }
        else if (wp == TIMER_CALREM) { KillTimer(hwnd, TIMER_CALREM); CheckCalReminders(NowMinute()); if (!g_hidden) Render(); }
        else if (wp == TIMER_WATER) { KillTimer(hwnd, TIMER_WATER); OnWaterTimer(); if (!g_hidden) Render(); }
        else if (wp == TIMER_POMO) {
            if (PomoLeftMs() > 0) PomoArm(); // fired a little early: wait for the rest
            else { KillTimer(hwnd, TIMER_POMO); PomoPhaseEnded(); }
            if (!g_hidden) Render();
        }
        else if (wp == TIMER_PEEK) {
            KillTimer(hwnd, TIMER_PEEK);
            if (g_hover) { g_hoverOpen = true; g_peekTarget = 0; if (!g_flash) g_toast = false; UpdateTarget(); }
        }
        else if (wp == TIMER_PET_IDLE) OnPetIdleTimer();
        else if (wp == TIMER_CAL) CalFetch(false);
        else if (wp == TIMER_PET_ANIM) OnPetFrame();
        else if (wp == TIMER_CAPS) { KillTimer(hwnd, TIMER_CAPS); ShowCapsToast(); }
        else if (wp == TIMER_LANG) { KillTimer(hwnd, TIMER_LANG); CheckInputLanguage(); }
        else if (wp == TIMER_MINUTE) { ArmMinuteTimer(); if (!g_hidden && g_t == 0) Render(); }
        else if (wp == TIMER_FLASH) { KillTimer(hwnd, TIMER_FLASH); g_flash = false; UpdateTarget(); }
        else if (wp == TIMER_SETTINGS) {          // settle time after a save (editors write more than once)
            KillTimer(hwnd, TIMER_SETTINGS);
            FILETIME now = SettingsStamp();
            if (CompareFileTime(&now, &g_setStamp) != 0) ApplySettings(true); // ignore other files (shelf.txt)
        }
        return 0;
    case WM_APP_SETTINGS_CHANGED:
        SetTimer(hwnd, TIMER_SETTINGS, 200, nullptr);
        return 0;
    case WM_APP_SETTING:
        switch (wp) {
        case SQ_WIDTH: return g_set.width;
        case SQ_HEIGHT: return g_set.height;
        case SQ_ROUNDNESS: return g_set.roundness;
        case SQ_LOADS: return g_setLoads;
        case SQ_STYLE: return g_set.style;
        case SQ_GLASS: return g_set.glass;
        case SQ_SIZE: return g_set.size;
        case SQ_MINIMAL: return g_set.minimal;
        case SQ_CLOCK: return g_set.clock;
        case SQ_RING: return g_set.ring;
        case SQ_SCALE_X1000: return lroundf(g_scale * 1000);
        }
        return -1;
    case WM_APP_PILL: {
        RECT wr;
        GetWindowRect(hwnd, &wr);
        if (wp == 0) return MAKELONG(wr.left + (int)std::lround(g_pill.left * g_scale), wr.top + (int)std::lround((g_pill.top + g_offY) * g_scale));
        return MAKELONG(wr.left + (int)std::lround(g_pill.right * g_scale), wr.top + (int)std::lround((g_pill.bottom + g_offY) * g_scale));
    }
    case WM_APP_PAGE:
        if ((int)wp >= 0 && (int)wp < PAGE_COUNT) { GoPage((int)wp, 1); g_pageT = 1; Render(); }
        return g_page;
    case WM_APP_HOT: {
        D2D1_POINT_2F c = HotCenter((int)wp);
        return MAKELONG((int)std::lround(c.x * g_scale), (int)std::lround((c.y + g_offY) * g_scale));
    }
    case WM_APP_APPBAR:
        if (wp == ABN_FULLSCREENAPP) SetFullscreenHidden(lp && g_set.hideFullscreen ? ForegroundIsFullscreenOnPrimary() : false); // "Step aside for full screen"
        return 0;
    case WM_APP_STATE:
        return GetStateFlags();
    case WM_APP_MEDIA:
        OnMediaChanged();
        return 0;
    case WM_APP_VOLUME: { // 3.3
        float level = wp / 1000.0f;
        bool muted = lp != 0;
        if (GetTickCount64() - g_selfMuteAt < 600) { Render(); return 0; } // from the Music page's speaker button
        const wchar_t* glyph = muted ? L"\xE74F"                        // Mute
                             : g_outIsHeadphones ? L"\xE7F6"           // Headphone
                             : level < 0.01f ? L"\xE992"               // Volume0
                             : level < 0.34f ? L"\xE993" : level < 0.67f ? L"\xE994" : L"\xE995";
        if (g_set.popVolume) ShowToast(muted ? L"Muted" : L"Volume", g_outName, glyph, level, muted);
        return 0;
    }
    case WM_APP_OUTPUT_CHANGED: { // 3.6
        float level = AttachDefaultOutput();
        BOOL muted = FALSE;
        if (g_outVol) g_outVol->GetMute(&muted);
        if (g_set.popVolume && GetTickCount64() - g_btShownAt > 3000) // a Bluetooth "Connected" pop-up already said it
            ShowToast(g_outIsHeadphones ? L"Headphones" : L"Sound output", g_outName[0] ? g_outName : L"Default device",
                  g_outIsHeadphones ? L"\xE7F6" : L"\xE7F5", level < 0 ? 0 : level, muted != FALSE);
        return 0;
    }
    case WM_APP_KEY:
        if (wp == VK_CAPITAL) { if (g_set.popCaps) SetTimer(hwnd, TIMER_CAPS, 30, nullptr); } // once Windows has toggled it
        else SetTimer(hwnd, TIMER_LANG, 150, nullptr);                                    // once the switch has happened
        return 0;
    case WM_APP_BT_DEVICE:
        OnAudioDeviceActive((wchar_t*)lp);
        return 0;
    case WM_APP_WIFI:
        OnWifi(wp != 0, (wchar_t*)lp);
        return 0;
    case WM_APP_SIM_LANG:
        ShowLanguageToast((LANGID)wp);
        return 0;
    case WM_APP_BT_COUNT:
        return CountBluetoothOutputs();
    case WM_APP_TODO: {
        if (wp == 1) { g_testMinute = (int)lp; CheckTaskReminders((int)lp); if (!g_hidden) Render(); return 0; }
        if (wp == 2) return (LRESULT)g_remQueue.size();
        int today = TodayStamp(), done = 0;
        for (const Task& t : g_tasks) done += TaskDone(t, today);
        return (LRESULT)g_tasks.size() | (done << 8) | (g_edit << 16) | ((g_act ? g_rem.kind : 0) << 20) | (g_todoEditMode << 24);
    }
    case WM_APP_CAL_DONE:
        OnCalDone((CalJob*)lp);
        return 0;
    case WM_APP_ASK_DONE:
        OnAskDone((AskJob*)lp);
        return 0;
    case WM_HOTKEY:
        if (wp == kAskHotkeyId) { if (g_ask && GetForegroundWindow() == g_hwnd) AskClose(); else AskOpen(); }
        return 0;
    case WM_APP_ASK:
        if (wp == 1) { AskOpen(); return 1; }
        if (wp == 2) return (LRESULT)g_askModels.size();
        if (wp == 3) { AskClose(); return 0; }
        if (wp == 4) { // tests: save the key held in global atom lp (as Paste key would, without the clipboard)
            wchar_t k[256] = L"";
            if (!GlobalGetAtomNameW((ATOM)lp, k, 256) || !AskSaveKey(k)) return 0;
            SecureZeroMemory(k, sizeof k);
            g_askModels.clear();
            g_askModelsTried = false;
            AskFetchModels();
            return 1;
        }
        return (g_ask ? 1 : 0) | (g_askBusy ? 2 : 0) | (AskHasKey() ? 4 : 0) | (g_askError.empty() ? 0 : 8) | ((LRESULT)std::min<size_t>(g_askAnswer.size(), 0xFFFFFF) << 8);
    case WM_APP_TRAY: // NOTIFYICON_VERSION_4: the event is in LOWORD(lp), the point in wp
        if (LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT || LOWORD(lp) == WM_CONTEXTMENU) {
            POINT pt = {GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
            ShowTrayMenu(pt);
        }
        return 0;
    case WM_APP_STRAY: {
        if (!g_bits) return -1;
        GdiFlush();
        LONG l = (LONG)std::floor(g_lastDrawn.left * g_scale), r = (LONG)std::ceil(g_lastDrawn.right * g_scale), b = (LONG)std::ceil(g_lastDrawn.bottom * g_scale);
        LRESULT n = 0;
        for (int y = 0; y < g_pxH; y++)
            for (int x = 0; x < g_pxW; x++)
                if ((g_bits[y * g_pxW + x] >> 24) && (y >= b || x < l || x >= r)) n++;
        return n;
    }
    case WM_CLIPBOARDUPDATE:
        OnClipboardChanged();
        if (!g_hidden && g_page == PAGE_SHELF && g_t > 0) Render();
        return 0;
    case WM_APP_CLIP:
        if (wp == 1) { g_shelfTab = lp ? 1 : 0; g_clipScroll = 0; if (!g_hidden) Render(); }
        return (LRESULT)g_clip.size();
    case WM_APP_WATER:
        if (wp == 0) return WaterToday();
        if (wp == 1) { WaterAdd((int)lp); if (!g_hidden) Render(); return WaterToday(); }
        if (wp == 2) { g_testMinute = (int)lp; OnWaterTimer(); if (!g_hidden) Render(); return g_act && g_rem.kind == 3 ? 1 : 0; }
        if (wp == 3) return WaterNextDue(NowMinute());
        return 0;
    case WM_APP_CAL: {
        int today = Cal_Today();
        if (wp == 1) { std::vector<CalItem> items; CalItemsOn(today + (int)lp, items); return (LRESULT)items.size(); }
        if (wp == 2) return (g_todoCal ? 1 : 0) | (g_calWeekView ? 2 : 0) | ((g_calSel - today + 1000) << 4);
        if (wp == 3) { CalFetch(false); return 0; }
        if (wp == 4) { int y, m, d; Cal_Date(g_calWeekView ? CalWeekStart(g_calAnchor) : g_calAnchor, y, m, d); return y * 100 + m; }
        if (wp == 5) { g_testMinute = (int)lp; CheckCalReminders(NowMinute()); if (!g_hidden) Render(); return g_act && g_rem.kind == 4 ? 1 : 0; }
        if (wp == 6) return CalRemNextDue(NowMinute());
        return g_calStatus | (std::min((int)g_calEvents.size(), 0xFFFF) << 4);
    }
    case WM_INPUT: // mouse or keyboard anywhere: wake the mascot
        PetWake();
        break;
    case WM_APP_PET:
        if (wp == 1) {
            if (g_petMode != PET_OFF || !PetCanAppear()) return 0;
            KillTimer(hwnd, TIMER_PET_IDLE);
            PetStart();
            return 1;
        }
        return g_petMode | (lroundf(PetAlpha() * 100) << 4);
    case WM_APP_POMO:
        if (wp == 1) { if (g_pomoState == POMO_RUNNING) { KillTimer(hwnd, TIMER_POMO); PomoPhaseEnded(); } return 0; }
        if (wp == 2) return g_chimes;
        if (wp == 3) return FocusMinutesToday();
        return g_pomoState | (g_pomoPhase << 2) | (g_pomoSession << 4) | (LRESULT)(((PomoLeftMs() + 999) / 1000) << 8);
    case WM_APP_TOAST_LEVEL:
        return (g_toast && g_toastLevel >= 0) ? lroundf(g_toastLevel * 1000) : -1;
    case WM_POWERBROADCAST: // 3.4 / 3.5
        if (wp == PBT_POWERSETTINGCHANGE && lp) OnPowerSettingChange((const POWERBROADCAST_SETTING*)lp);
        return TRUE;
    case WM_APP_BATTERY:
        return g_battPct < 0 ? -1 : ((g_onAC == 1 ? 1 : 0) << 8) | g_battPct;
    case WM_APP_SIM_POWER:
        OnPowerReport((int)wp, (int)lp);
        return 0;
    case WM_APP_BRIGHTNESS:
        return g_brightness;
    case WM_TIMECHANGE: // clock or time zone changed: the closed pill's time follows at once
        ArmMinuteTimer();
        CheckTaskReminders(NowMinute());
        ArmWaterTimer();
        CheckCalReminders(NowMinute());
        if (!g_hidden) Render();
        return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
        g_scale = WantedScale();
        CreateSurface();
        PlaceWindow();
        Render();
        return 0;
    case WM_DESTROY: {
        APPBARDATA abd = {sizeof(abd)};
        abd.hWnd = hwnd;
        SHAppBarMessage(ABM_REMOVE, &abd);
        RevokeDragDrop(hwnd);
        RemoveTrayIcon();
        StopIndicators();
        SetGlass(false);
        PostQuitMessage(0);
        return 0;
    }
    }
    if (msg == g_msgTaskbarCreated && msg) AddTrayIcon(); // Explorer restarted: put the tray icon back
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;
    // "Perch.exe --shelf <files...>": put files on the shelf (of the running notch if there is one).
    std::wstring shelfArgs;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc >= 2 && _wcsicmp(argv[1], L"--shelf") == 0) {
            for (int i = 2; i < argc; i++) {
                wchar_t full[MAX_PATH * 4];
                if (GetFullPathNameW(argv[i], MAX_PATH * 4, full, nullptr)) { shelfArgs += full; shelfArgs += L"\n"; }
            }
        }
        LocalFree(argv);
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\Perch.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND running = FindWindowW(L"PerchWindow", nullptr);
        if (running && !shelfArgs.empty()) {
            COPYDATASTRUCT cds = {1, (DWORD)(shelfArgs.size() * sizeof(wchar_t)), (void*)shelfArgs.data()};
            SendMessageW(running, WM_COPYDATA, 0, (LPARAM)&cds);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr); // COM (STA) + OLE drag and drop

    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, kIID_ID2D1Factory, &opts, (void**)&g_d2d))) return 1;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, kIID_IDWriteFactory, (IUnknown**)&g_dw))) return 1;
    CreateFormats();
    g_d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                         D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_DASH),
                             nullptr, 0, &g_dash);
    g_d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                             nullptr, 0, &g_round);
    LoadShelf();
    LoadTasks();
    LoadWater();

    WNDCLASSW wc = {};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PerchWindow";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"Perch", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    g_petOffEnv = GetEnvironmentVariableW(L"PERCH_MASCOT_OFF", nullptr, 0) != 0; // tests that look at the clock pill
    g_scale = WantedScale();
    CreateSurface();
    PlaceWindow();
    ApplySettings(false); // pill size, style, glass
    if (g_set.shelfEmpty && !g_shelf.empty()) { g_shelf.clear(); SaveShelf(); } // "Empty shelf at start"
    CheckTaskReminders(NowMinute());
    Render();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    Settings_Watch(DataDir(), g_hwnd, WM_APP_SETTINGS_CHANGED);
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();

    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = g_hwnd;
    abd.uCallbackMessage = WM_APP_APPBAR;
    SHAppBarMessage(ABM_NEW, &abd); // only for ABN_FULLSCREENAPP; we never reserve screen space

    StartAudioNotifications();
    StartIndicators();
    RegisterPowerNotifications();
    Media_Start(g_hwnd, WM_APP_MEDIA);

    g_dropTarget = new DropTarget();
    RegisterDragDrop(g_hwnd, g_dropTarget);
    if (!shelfArgs.empty()) AddShelfFromText(shelfArgs.data(), shelfArgs.size());

    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Settings_StopWatch();
    Media_Stop();
    StopAudioNotifications();
    UnregisterPowerNotifications();
    if (g_toastAtom) GlobalDeleteAtom(g_toastAtom);
    SafeRelease(g_dropTarget);
    ReleaseSurface();
    SafeRelease(g_fmtTime); SafeRelease(g_fmtDate); SafeRelease(g_fmtIcon); SafeRelease(g_fmtSmall); SafeRelease(g_fmtPill); SafeRelease(g_fmtPillTime);
    SafeRelease(g_fmtTitle); SafeRelease(g_fmtToast); SafeRelease(g_fmtArtist); SafeRelease(g_fmtCtl); SafeRelease(g_fmtMeta);
    SafeRelease(g_fmtTile); SafeRelease(g_fmtDrop); SafeRelease(g_dash); SafeRelease(g_round); SafeRelease(g_wic);
    SafeRelease(g_fmtHdr); SafeRelease(g_fmtHdrDate); SafeRelease(g_fmtHdrTime); SafeRelease(g_fmtChevron);
    SafeRelease(g_fmtBarIcon); SafeRelease(g_fmtPlay);
    SafeRelease(g_fmtPomoTime); SafeRelease(g_fmtPomoSub); SafeRelease(g_fmtPomoBtn); SafeRelease(g_fmtPomoLabel); SafeRelease(g_fmtPomoState); SafeRelease(g_fmtPomoSmall); SafeRelease(g_fmtPomoFoot);
    for (IDWriteTextFormat** f : {&g_fmtTodoCount, &g_fmtTodoBtn, &g_fmtTodoRow, &g_fmtTodoTime, &g_fmtTodoSmall, &g_fmtTodoField,
                                  &g_fmtActTitle, &g_fmtActSub, &g_fmtActBtn}) SafeRelease(*f);
    SafeRelease(g_dw); SafeRelease(g_d2d);
    OleUninitialize();
    if (single) CloseHandle(single);
    return 0;
}
