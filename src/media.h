// Now playing + media controls, via Windows' Global System Media Transport
// Controls (the same source as the volume flyout's media panel). Works with
// Spotify, browsers (YouTube etc.), Media Player and any app that publishes
// its playback to Windows.
//
// All WinRT work happens on one background thread, driven by events.
#pragma once
#include <windows.h>
#include <vector>

struct MediaInfo {
    bool hasSession = false;
    bool playing = false;
    wchar_t title[256] = L"";
    wchar_t artist[256] = L"";
    int artSize = 0;          // artwork is artSize x artSize, 32bpp premultiplied BGRA
    std::vector<BYTE> art;    // empty when the app gives no artwork
    unsigned artVersion = 0;  // bumps whenever the artwork changes
    // Timeline (100 ns units). posAt = FILETIME (UTC) when `pos` was true; 0 length when the app gives none.
    INT64 pos = 0, length = 0, posAt = 0;
};

// Current position (100 ns units), moving on from the last report while playing.
INT64 Media_Position(const MediaInfo& m);

enum MediaCommand { MEDIA_PLAYPAUSE = 1, MEDIA_NEXT, MEDIA_PREV };

// Starts the worker; posts `msg` to `notify` whenever MediaInfo changes.
void Media_Start(HWND notify, UINT msg);
void Media_Stop();
void Media_Get(MediaInfo& out);
void Media_Command(MediaCommand cmd);
