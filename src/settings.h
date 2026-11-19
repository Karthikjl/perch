// 1.5 Settings file: %APPDATA%\Perch\settings.ini (or %PERCH_DATA_DIR%).
// Missing file or missing lines mean defaults. The file is watched, so a save
// applies straight away (event-driven, no polling).
#pragma once
#include <windows.h>
#include <string>

enum NotchStyle { STYLE_NOTCH = 0, STYLE_ISLAND }; // Classic (joined to the top edge) / Floating (just below it)
enum NotchSize { SIZE_AUTO = 0, SIZE_100, SIZE_115, SIZE_130, SIZE_150 }; // "Notch size": scales the whole notch
enum RingKind { RING_BATTERY = 0, RING_NONE, RING_TASKS, RING_WATER };               // "Ring next to the clock"

struct Settings {
    int width = 150, height = 30, roundness = 14; // closed pill, DIPs (150 x 30, corners 14)
    int style = STYLE_NOTCH;
    bool glass = false;   // frosted glass behind the open notch
    int size = SIZE_AUTO; // Auto = 100%
    bool minimal = false; // a smaller pill when nothing is playing
    bool clock = true;    // "Date & time": the clock beside the status ring on the closed pill
    int ring = RING_BATTERY;
    bool mascot = true;   // the mascot in the closed notch when you leave the PC alone
    int mascotStyle = 0;  // MascotStyle (mascot.h): Falcon, Ember, Mint, Cyclops, Visor, Sunny, Moonlight
    // "HUD & popups": indicators and pop-ups that can each be switched off. [Popups] in settings.ini
    bool popVolume = true, popBrightness = true, popCaps = true, popLanguage = true;
    bool popBattery = true, popWifi = true, popBluetooth = true;
    int pomoFocus = 25, pomoShort = 5, pomoLong = 15; // Pomodoro durations, minutes. [Pomodoro]
    bool sounds = true;                               // "Reminder sounds": chimes. [Sounds] Reminders
    bool todoStart = true, todoEnd = true;            // To-do reminders: when a task starts / when time is up. [ToDo]
    int snooze = 10;                                  // "Snooze for": 5, 10 or 15 minutes
    std::wstring calLink;                             // Google Calendar: secret address in iCal format. [Calendar] Link
    bool weekMonday = false;                          // "Week starts on": Sunday / Monday
    bool calWeek = false;
    int calRemind = 10;                               // [Calendar] Remind: minutes before an event, 0 = at its start, -1 = off                             // "Default view": Month / Week
    // Appearance and General extras
    bool hideFullscreen = true;  // "Hide in full screen"
    bool openClick = false;      // "Open the notch": Hover / Click
    bool pageIcons = true;       // "Page icons": show them when you hover the page dots
    bool fastAnim = false;       // "Animation speed": Normal / Fast
    int glassTint = 40;          // "Glass tint", percent (0-100, steps of 10)
    // Water: a goal for the day, the size of one drink, and reminders spread across the active hours. [Water]
    bool waterRemind = false;    // Reminders
    int waterGoal = 2000;        // Goal, ml: 1500, 2000, 2500 or 3000
    int waterCup = 250;          // Cup, ml: what one tap of "+" adds (100-1000)
    int waterFrom = 8 * 60, waterUntil = 17 * 60; // From / Until, minutes since midnight
    bool shelfEmpty = false;
    // Clipboard history in the Shelf. [Clipboard]
    std::wstring askModel = L"gemini-flash-latest"; // [Ask] Model: the Gemini model questions go to (picked in Settings)
    bool askHotkey = true;                        // [Ask] Hotkey: Ctrl + Alt + Space opens Ask Perch
    bool clipHistory = false;    // History: keep the text you copy
    int clipMax = 20;            // Max: items kept, 5-50
    bool clipRemember = false;   // Remember: keep them after a restart (encrypted for this Windows account)     // Shelf: "Empty shelf at start"
    std::wstring pageOrder = L"music,pomodoro,water,shelf,todo"; // [Pages] Order
    std::wstring pagesHidden = L"widgets";                        // [Pages] Hidden
};

float Settings_SizeFactor(const Settings& s); // 1.0, 1.15, 1.3 or 1.5

// Pages the notch can show, in their default order. Names are what [Pages] Order / Hidden use.
enum PageKind { PK_MUSIC = 0, PK_POMODORO, PK_SHELF, PK_TODO, PK_WATER, PK_WIDGETS, PK_COUNT };
extern const wchar_t* const kPageKeys[PK_COUNT];   // "music", "pomodoro", ...
extern const wchar_t* const kPageTitles[PK_COUNT]; // "Music", "Pomodoro", ...
// The pages shown, in order (hidden ones left out; unknown names ignored; never empty).
int Settings_PageOrder(const Settings& s, int* out); // returns how many
bool Settings_PageHidden(const Settings& s, int kind);
// Moves / shows / hides a page, rewriting pageOrder and pagesHidden.
void Settings_SetPages(Settings& s, const int* order, int n, const bool* hidden);

// Reads the file into `s` (starting from defaults). Returns "" when everything was
// understood, else a short description of the first problem (that value keeps its default
// or is clamped into range).
std::wstring Settings_Load(const std::wstring& file, Settings& s);
// Writes the commented default file if it doesn't exist yet. Never overwrites.
void Settings_CreateDefault(const std::wstring& file);
// Saves every value (creating the commented file first if needed). Values are
// changed in place, so comments and other lines stay as they are.
void Settings_Save(const std::wstring& file, const Settings& s);

// Posts `msg` to `hwnd` whenever something in `dir` changes (debounce on the receiving side).
void Settings_Watch(const std::wstring& dir, HWND hwnd, UINT msg);
void Settings_StopWatch();
