// Updates from GitHub: the newest release of Karthikjl/perch and its Perch.exe. Blocking calls, made from a
// worker thread. notch.cpp decides when to check and swaps the program file.
#pragma once
#include <string>

#define PERCH_VERSION L"0.1.0" // this build; keep in step with perch.rc

struct UpdateInfo { std::wstring version, url; }; // "0.2.0" and the address of its Perch.exe

// The releases API: https://api.github.com/repos/Karthikjl/perch/releases/latest, or PERCH_UPDATE_URL (tests only).
std::wstring Update_ApiUrl();
// Asks for the newest release. Returns the HTTP status (0: no connection); on 200, `out` says what it has
// (url empty when the release has no Perch.exe).
int Update_Check(UpdateInfo& out);
// true when version a is newer than b ("0.10.0" > "0.9.2"; a leading "v" is ignored).
bool Update_Newer(const std::wstring& a, const std::wstring& b);
// Downloads the new Perch.exe to `path`. Returns true only for a complete Windows program.
bool Update_Download(const std::wstring& url, const std::wstring& path);
