// 1.5 Settings file. See settings.h.
#include "settings.h"
#include "mascot.h"
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <initializer_list>

namespace {

const wchar_t* kDefaultFile =
    L"; Perch settings. Save this file and the changes apply straight away.\r\n"
    L"; Delete a line (or the whole file) to go back to the default.\r\n"
    L"\r\n"
    L"[Notch]\r\n"
    L"; Size of the closed pill, in pixels at 100% display scaling.\r\n"
    L"; Width 120-300, Height 20-40, Roundness 0-20 (how round the corners are).\r\n"
    L"Width=150\r\n"
    L"Height=30\r\n"
    L"Roundness=14\r\n"
    L"; Style: Classic (joined to the top edge) or Floating (just below it).\r\n"
    L"Style=Classic\r\n"
    L"; Glass: 1 for frosted glass behind the open notch.\r\n"
    L"Glass=0\r\n"
    L"; Size: Auto, 100%, 115%, 130% or 150% (makes the whole notch bigger).\r\n"
    L"Size=Auto\r\n"
    L"; Minimal: 1 hides the notch to a small tab while nothing is playing (right-click > Minimal notch).\r\n"
    L"Minimal=0\r\n"
    L"; Clock: 1 shows the time on the closed pill.\r\n"
    L"Clock=1\r\n"
    L"; Ring next to the clock: Battery, Water (today's water), Tasks (tasks done today) or None.\r\n"
    L"Ring=Battery\r\n"
    L"; Mascot: 1 for a little friend in the closed notch. It shows up when you leave the PC alone,\r\n"
    L"; dozes off, and wakes up when you come back.\r\n"
    L"Mascot=1\r\n"
    L"; MascotStyle: Falcon, Ember, Mint, Cyclops, Visor, Sunny or Moonlight.\r\n"
    L"MascotStyle=Falcon\r\n"
    L"\r\n"
    L"[Popups]\r\n"
    L"; 1 shows the pop-up, 0 turns it off.\r\n"
    L"Volume=1\r\n"
    L"Brightness=1\r\n"
    L"CapsLock=1\r\n"
    L"InputLanguage=1\r\n"
    L"Battery=1\r\n"
    L"WiFi=1\r\n"
    L"Bluetooth=1\r\n"
    L"\r\n"
    L"[Pomodoro]\r\n"
    L"; Minutes. A round is 4 focus sessions with short breaks between them, then a long break.\r\n"
    L"Focus=25\r\n"
    L"ShortBreak=5\r\n"
    L"LongBreak=15\r\n"
    L"\r\n"
    L"[Sounds]\r\n"
    L"; Reminders: 1 plays a chime for Pomodoro and To-do reminders.\r\n"
    L"Reminders=1\r\n"
    L"\r\n"
    L"[ToDo]\r\n"
    L"; Reminders when a task starts and when its time is up (1 on, 0 off).\r\n"
    L"RemindStart=1\r\n"
    L"RemindEnd=1\r\n"
    L"; Snooze: 5, 10 or 15 minutes (Later / +min on a reminder).\r\n"
    L"Snooze=10\r\n"
    L"\r\n"
    L"[Water]\r\n"
    L"; Reminders: 1 reminds you to drink, spread across the active hours, until you reach the goal.\r\n"
    L"Reminders=0\r\n"
    L"; Goal: ml for the day (1500, 2000, 2500 or 3000). Cup: ml that one tap of + adds (100-1000).\r\n"
    L"Goal=2000\r\n"
    L"Cup=250\r\n"
    L"; From / Until: the hours reminders come in, as HH:MM.\r\n"
    L"From=08:00\r\n"
    L"Until=17:00\r\n"
    L"\r\n"
    L"[Calendar]\r\n"
    L"; Link: your Google Calendar's \"Secret address in iCal format\" (Google Calendar > Settings >\r\n"
    L"; your calendar > Integrate calendar). Right-click the notch > Paste calendar link also sets it.\r\n"
    L"; Perch downloads it every 30 minutes; nothing else goes over the internet.\r\n"
    L"Link=\r\n"
    L"; WeekStart: Sunday or Monday. View: Month or Week (what the calendar opens on).\r\n"
    L"WeekStart=Sunday\r\n"
    L"View=Month\r\n"
    L"\r\n"
    L"[Appearance]\r\n"
    L"; HideInFullScreen: 1 steps aside for games, videos and slides that fill the screen.\r\n"
    L"HideInFullScreen=1\r\n"
    L"; Open: Hover (opens when the pointer rests on it) or Click.\r\n"
    L"Open=Hover\r\n"
    L"; PageIcons: 1 shows page icons when you hover the page dots.\r\n"
    L"PageIcons=1\r\n"
    L"; Animation: Normal or Fast. GlassTint: 0-100 (percent, with Glass on).\r\n"
    L"Animation=Normal\r\n"
    L"GlassTint=40\r\n"
    L"\r\n"
    L"[Pages]\r\n"
    L"; The pages in the open notch, in order, and the ones switched off.\r\n"
    L"; Names: music, pomodoro, water, shelf, todo, widgets.\r\n"
    L"Order=music,pomodoro,water,shelf,todo\r\n"
    L"Hidden=widgets\r\n"
    L"\r\n"
    L"[Shelf]\r\n"
    L"; StartEmpty: 1 clears the shelf each time Perch starts.\r\n"
    L"StartEmpty=0\r\n";

std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && iswspace(s[i])) i++;
    return s.substr(i);
}

std::wstring Read(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool& present) {
    wchar_t buf[256];
    // A sentinel default tells "missing" apart from "empty".
    GetPrivateProfileStringW(section, key, L"\x01", buf, 256, file.c_str());
    present = !(buf[0] == 1 && buf[1] == 0);
    return present ? Trim(buf) : L"";
}

void ReadInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int lo, int hi, int& value, std::wstring& problem) {
    bool present;
    std::wstring v = Read(file, section, key, present);
    if (!present) return;
    wchar_t* end = nullptr;
    long n = wcstol(v.c_str(), &end, 10);
    wchar_t msg[128];
    if (v.empty() || *end) {
        swprintf(msg, 128, L"%ls: \"%ls\" isn't a number", key, v.c_str());
        if (problem.empty()) problem = msg;
        return;
    }
    if (n < lo || n > hi) {
        swprintf(msg, 128, L"%ls: use %d-%d", key, lo, hi);
        if (problem.empty()) problem = msg;
        n = n < lo ? lo : hi;
    }
    value = (int)n;
}

// One of several words (case-insensitive) -> its index.
void ReadChoice(const std::wstring& file, const wchar_t* section, const wchar_t* key, std::initializer_list<const wchar_t*> words,
                int& value, std::wstring& problem) {
    bool present;
    std::wstring v = Read(file, section, key, present);
    if (!present) return;
    int i = 0;
    for (const wchar_t* w : words) {
        if (!_wcsicmp(v.c_str(), w)) { value = i; return; }
        i++;
    }
    if (problem.empty()) {
        std::wstring list;
        for (const wchar_t* w : words) list += (list.empty() ? L"" : L", ") + std::wstring(w);
        problem = std::wstring(key) + L": use " + list;
    }
}

void ReadBool(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool& value, std::wstring& problem) {
    int n = value ? 1 : 0;
    ReadInt(file, section, key, 0, 1, n, problem);
    value = n != 0;
}

// "HH:MM" -> minutes since midnight.
void ReadClock(const std::wstring& file, const wchar_t* section, const wchar_t* key, int& value, std::wstring& problem) {
    bool present;
    std::wstring v = Read(file, section, key, present);
    if (!present) return;
    int h = -1, m = -1;
    if (swscanf(v.c_str(), L"%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) { value = h * 60 + m; return; }
    if (problem.empty()) problem = std::wstring(section) + L": " + key + L" should be a time like 08:00";
}

const wchar_t* kStyles[] = {L"Classic", L"Floating"};
const wchar_t* kSizes[] = {L"Auto", L"100%", L"115%", L"130%", L"150%"};
const wchar_t* kRings[] = {L"Battery", L"None", L"Tasks", L"Water"};
const wchar_t* kSnoozes[] = {L"5", L"10", L"15"};
struct PopupKey { const wchar_t* key; bool Settings::*field; };
const PopupKey kPopups[] = {{L"Volume", &Settings::popVolume},     {L"Brightness", &Settings::popBrightness},
                            {L"CapsLock", &Settings::popCaps},     {L"InputLanguage", &Settings::popLanguage},
                            {L"Battery", &Settings::popBattery},   {L"WiFi", &Settings::popWifi},
                            {L"Bluetooth", &Settings::popBluetooth}};

HANDLE g_watchThread, g_watchStop;
struct WatchArgs { std::wstring dir; HWND hwnd; UINT msg; };

DWORD WINAPI WatchThread(LPVOID p) {
    WatchArgs* a = (WatchArgs*)p;
    HANDLE change = FindFirstChangeNotificationW(a->dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    if (change != INVALID_HANDLE_VALUE) {
        HANDLE hs[2] = {g_watchStop, change};
        while (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            PostMessageW(a->hwnd, a->msg, 0, 0);
            if (!FindNextChangeNotification(change)) break;
        }
        FindCloseChangeNotification(change);
    }
    delete a;
    return 0;
}

} // namespace

float Settings_SizeFactor(const Settings& s) {
    static const float k[] = {1.0f, 1.0f, 1.15f, 1.3f, 1.5f};
    return k[s.size];
}

std::wstring Settings_Load(const std::wstring& file, Settings& s) {
    s = Settings();
    std::wstring problem;
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return problem;
    ReadInt(file, L"Notch", L"Width", 120, 300, s.width, problem);
    ReadInt(file, L"Notch", L"Height", 20, 40, s.height, problem);
    ReadInt(file, L"Notch", L"Roundness", 0, 20, s.roundness, problem);
    ReadChoice(file, L"Notch", L"Style", {kStyles[0], kStyles[1]}, s.style, problem);
    ReadBool(file, L"Notch", L"Glass", s.glass, problem);
    ReadChoice(file, L"Notch", L"Size", {kSizes[0], kSizes[1], kSizes[2], kSizes[3], kSizes[4]}, s.size, problem);
    ReadBool(file, L"Notch", L"Minimal", s.minimal, problem);
    ReadBool(file, L"Notch", L"Clock", s.clock, problem);
    ReadChoice(file, L"Notch", L"Ring", {kRings[0], kRings[1], kRings[2], kRings[3]}, s.ring, problem);
    ReadBool(file, L"Notch", L"Mascot", s.mascot, problem);
    ReadChoice(file, L"Notch", L"MascotStyle", {kMascotKeys[0], kMascotKeys[1], kMascotKeys[2], kMascotKeys[3], kMascotKeys[4],
               kMascotKeys[5], kMascotKeys[6]}, s.mascotStyle, problem);
    for (const PopupKey& k : kPopups) ReadBool(file, L"Popups", k.key, s.*(k.field), problem);
    ReadBool(file, L"Water", L"Reminders", s.waterRemind, problem);
    {
        int g = s.waterGoal == 1500 ? 0 : s.waterGoal == 2500 ? 2 : s.waterGoal == 3000 ? 3 : 1;
        ReadChoice(file, L"Water", L"Goal", {L"1500", L"2000", L"2500", L"3000"}, g, problem);
        s.waterGoal = 1500 + 500 * g;
    }
    ReadInt(file, L"Water", L"Cup", 100, 1000, s.waterCup, problem);
    ReadClock(file, L"Water", L"From", s.waterFrom, problem);
    ReadClock(file, L"Water", L"Until", s.waterUntil, problem);
    if (s.waterUntil <= s.waterFrom) s.waterUntil = std::min(s.waterFrom + 60, 24 * 60 - 1);
    ReadInt(file, L"Pomodoro", L"Focus", 1, 120, s.pomoFocus, problem);
    ReadInt(file, L"Pomodoro", L"ShortBreak", 1, 60, s.pomoShort, problem);
    ReadInt(file, L"Pomodoro", L"LongBreak", 1, 60, s.pomoLong, problem);
    ReadBool(file, L"Sounds", L"Reminders", s.sounds, problem);
    ReadBool(file, L"ToDo", L"RemindStart", s.todoStart, problem);
    ReadBool(file, L"ToDo", L"RemindEnd", s.todoEnd, problem);
    int sn = s.snooze == 5 ? 0 : s.snooze == 15 ? 2 : 1;
    ReadChoice(file, L"ToDo", L"Snooze", {kSnoozes[0], kSnoozes[1], kSnoozes[2]}, sn, problem);
    s.snooze = sn == 0 ? 5 : sn == 2 ? 15 : 10;
    wchar_t link[2048] = L"";
    GetPrivateProfileStringW(L"Calendar", L"Link", L"", link, 2048, file.c_str());
    s.calLink = Trim(link);
    int ws = 0, view = 0;
    ReadChoice(file, L"Calendar", L"WeekStart", {L"Sunday", L"Monday"}, ws, problem);
    ReadChoice(file, L"Calendar", L"View", {L"Month", L"Week"}, view, problem);
    s.weekMonday = ws == 1;
    s.calWeek = view == 1;
    ReadBool(file, L"Appearance", L"HideInFullScreen", s.hideFullscreen, problem);
    int open = s.openClick ? 1 : 0, anim = s.fastAnim ? 1 : 0;
    ReadChoice(file, L"Appearance", L"Open", {L"Hover", L"Click"}, open, problem);
    s.openClick = open == 1;
    ReadBool(file, L"Appearance", L"PageIcons", s.pageIcons, problem);
    ReadChoice(file, L"Appearance", L"Animation", {L"Normal", L"Fast"}, anim, problem);
    s.fastAnim = anim == 1;
    ReadInt(file, L"Appearance", L"GlassTint", 0, 100, s.glassTint, problem);
    bool present;
    std::wstring order = Read(file, L"Pages", L"Order", present);
    if (present) s.pageOrder = order;
    std::wstring hidden = Read(file, L"Pages", L"Hidden", present);
    if (present) s.pagesHidden = hidden;
    ReadBool(file, L"Shelf", L"StartEmpty", s.shelfEmpty, problem);
    return problem;
}

void Settings_CreateDefault(const std::wstring& file) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return; // already there (or can't write): leave it
    // Plain ASCII, so Notepad and the INI reader agree on it.
    std::string ascii;
    for (const wchar_t* p = kDefaultFile; *p; p++) ascii += (char)*p;
    DWORD w;
    WriteFile(f, ascii.data(), (DWORD)ascii.size(), &w, nullptr);
    CloseHandle(f);
}

void Settings_Save(const std::wstring& file, const Settings& s) {
    Settings_CreateDefault(file);
    const wchar_t* f = file.c_str();
    WritePrivateProfileStringW(L"Notch", L"Width", std::to_wstring(s.width).c_str(), f);
    WritePrivateProfileStringW(L"Notch", L"Height", std::to_wstring(s.height).c_str(), f);
    WritePrivateProfileStringW(L"Notch", L"Roundness", std::to_wstring(s.roundness).c_str(), f);
    WritePrivateProfileStringW(L"Notch", L"Style", kStyles[s.style], f);
    WritePrivateProfileStringW(L"Notch", L"Glass", s.glass ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Notch", L"Size", kSizes[s.size], f);
    WritePrivateProfileStringW(L"Notch", L"Minimal", s.minimal ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Notch", L"Clock", s.clock ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Notch", L"Ring", kRings[s.ring], f);
    WritePrivateProfileStringW(L"Notch", L"Mascot", s.mascot ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Notch", L"MascotStyle", kMascotKeys[s.mascotStyle], f);
    for (const PopupKey& k : kPopups) WritePrivateProfileStringW(L"Popups", k.key, s.*(k.field) ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Pomodoro", L"Focus", std::to_wstring(s.pomoFocus).c_str(), f);
    WritePrivateProfileStringW(L"Pomodoro", L"ShortBreak", std::to_wstring(s.pomoShort).c_str(), f);
    WritePrivateProfileStringW(L"Pomodoro", L"LongBreak", std::to_wstring(s.pomoLong).c_str(), f);
    WritePrivateProfileStringW(L"Water", L"Reminders", s.waterRemind ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Water", L"Goal", std::to_wstring(s.waterGoal).c_str(), f);
    WritePrivateProfileStringW(L"Water", L"Cup", std::to_wstring(s.waterCup).c_str(), f);
    {
        wchar_t t[8];
        swprintf(t, 8, L"%02d:%02d", s.waterFrom / 60, s.waterFrom % 60);
        WritePrivateProfileStringW(L"Water", L"From", t, f);
        swprintf(t, 8, L"%02d:%02d", s.waterUntil / 60, s.waterUntil % 60);
        WritePrivateProfileStringW(L"Water", L"Until", t, f);
    }
    WritePrivateProfileStringW(L"Sounds", L"Reminders", s.sounds ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"ToDo", L"RemindStart", s.todoStart ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"ToDo", L"RemindEnd", s.todoEnd ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"ToDo", L"Snooze", std::to_wstring(s.snooze).c_str(), f);
    WritePrivateProfileStringW(L"Calendar", L"Link", s.calLink.c_str(), f);
    WritePrivateProfileStringW(L"Calendar", L"WeekStart", s.weekMonday ? L"Monday" : L"Sunday", f);
    WritePrivateProfileStringW(L"Calendar", L"View", s.calWeek ? L"Week" : L"Month", f);
    WritePrivateProfileStringW(L"Appearance", L"HideInFullScreen", s.hideFullscreen ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Appearance", L"Open", s.openClick ? L"Click" : L"Hover", f);
    WritePrivateProfileStringW(L"Appearance", L"PageIcons", s.pageIcons ? L"1" : L"0", f);
    WritePrivateProfileStringW(L"Appearance", L"Animation", s.fastAnim ? L"Fast" : L"Normal", f);
    WritePrivateProfileStringW(L"Appearance", L"GlassTint", std::to_wstring(s.glassTint).c_str(), f);
    WritePrivateProfileStringW(L"Pages", L"Order", s.pageOrder.c_str(), f);
    WritePrivateProfileStringW(L"Pages", L"Hidden", s.pagesHidden.c_str(), f);
    WritePrivateProfileStringW(L"Shelf", L"StartEmpty", s.shelfEmpty ? L"1" : L"0", f);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, f); // flush
}

const wchar_t* const kPageKeys[PK_COUNT] = {L"music", L"pomodoro", L"shelf", L"todo", L"water", L"widgets"};
const wchar_t* const kPageTitles[PK_COUNT] = {L"Music", L"Pomodoro", L"Shelf", L"To-do", L"Water", L"Widgets"};

static int PageFromName(const std::wstring& w) {
    for (int k = 0; k < PK_COUNT; k++) if (!_wcsicmp(w.c_str(), kPageKeys[k])) return k;
    return -1;
}
static void SplitNames(const std::wstring& list, int* out, int& n) { // comma-separated names -> kinds, no repeats
    n = 0;
    size_t p = 0;
    while (p <= list.size()) {
        size_t c = list.find(L',', p);
        std::wstring w = Trim(list.substr(p, c == std::wstring::npos ? std::wstring::npos : c - p));
        p = c == std::wstring::npos ? list.size() + 1 : c + 1;
        int k = PageFromName(w);
        bool seen = false;
        for (int i = 0; i < n; i++) seen |= out[i] == k;
        if (k >= 0 && !seen && n < PK_COUNT) out[n++] = k;
    }
}
bool Settings_PageHidden(const Settings& s, int kind) {
    int h[PK_COUNT], nh;
    SplitNames(s.pagesHidden, h, nh);
    for (int i = 0; i < nh; i++) if (h[i] == kind) return true;
    return false;
}
int Settings_PageOrder(const Settings& s, int* out) {
    int all[PK_COUNT], n, m = 0;
    SplitNames(s.pageOrder, all, n);
    static const int deflt[] = {PK_MUSIC, PK_POMODORO, PK_WATER, PK_SHELF, PK_TODO, PK_WIDGETS}; // pages left out of Order go last
    for (int k : deflt) {
        bool in = false;
        for (int i = 0; i < n; i++) in |= all[i] == k;
        if (!in) all[n++] = k;
    }
    for (int i = 0; i < n; i++) if (!Settings_PageHidden(s, all[i])) out[m++] = all[i];
    if (m == 0) out[m++] = PK_MUSIC;
    return m;
}
void Settings_SetPages(Settings& s, const int* order, int n, const bool* hidden) {
    s.pageOrder.clear();
    s.pagesHidden.clear();
    for (int i = 0; i < n; i++) {
        std::wstring& list = hidden[order[i]] ? s.pagesHidden : s.pageOrder;
        if (!list.empty()) list += L",";
        list += kPageKeys[order[i]];
    }
}

void Settings_Watch(const std::wstring& dir, HWND hwnd, UINT msg) {
    if (g_watchThread) return;
    g_watchStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_watchThread = CreateThread(nullptr, 64 * 1024, WatchThread, new WatchArgs{dir, hwnd, msg}, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
}

void Settings_StopWatch() {
    if (!g_watchThread) return;
    SetEvent(g_watchStop);
    WaitForSingleObject(g_watchThread, 1000);
    CloseHandle(g_watchThread);
    CloseHandle(g_watchStop);
    g_watchThread = g_watchStop = nullptr;
}
