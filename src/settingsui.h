// Settings window (tray icon or right-click -> Settings..., or the gear on the page icons), laid out
// a sidebar of sections on the left, cards of switches and choices on the right.
// Every change is saved to settings.ini straight away; the window only exists while open.
#pragma once
#include <windows.h>
#include "settings.h"
#include <vector>

struct SettingsUIHost {
    void (*commit)(const Settings& s);          // save to settings.ini and apply
    void (*openFile)();                         // "Edit file": settings.ini in Notepad
    bool (*getAutostart)();                     // Start with Windows (registry, kept by the notch)
    void (*setAutostart)(bool on);
    void (*calPaste)();                         // To-do > Google Calendar: Paste link / Refresh / Remove
    void (*calRefresh)();
    void (*calRemove)();
    void (*calStatus)(wchar_t* out, int n);     // "Not connected", "Connected · 12 events · Updated 14:02"
    void (*waterStatus)(wchar_t* out, int n);   // Water > Today: "1.25 L of 2 L"
    void (*askPaste)();                         // Ask Perch > Gemini API key: Paste key / Remove
    void (*askRemove)();
    void (*askStatus)(wchar_t* out, int n);     // "Not set", "Saved · 12 models"
    void (*askModels)(std::vector<std::wstring>& out); // the models the key may use (empty until Google answers)
};

// Sections, in sidebar order.
enum SettingsSection { SEC_GENERAL = 0, SEC_DISPLAY, SEC_PAGES, SEC_APPEARANCE, SEC_MUSIC, SEC_WIDGETS, SEC_ASK, SEC_TODO,
                       SEC_POMODORO, SEC_WATER, SEC_SHELF, SEC_HUD, SEC_ABOUT, SEC_COUNT };

void SettingsUI_Show(HINSTANCE inst, const Settings& current, const SettingsUIHost& host, int section = -1);
void SettingsUI_Refresh(const Settings& s); // the file changed on disk (or calendar status) while the window is open
bool SettingsUI_IsOpen();
