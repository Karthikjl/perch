// Synced lyrics from LRCLIB (lrclib.net), a free community lyrics database. Only used when the user turns
// lyrics on; sends the song's title, artist and length. Blocking; called from a worker thread.
#pragma once
#include <string>
#include <vector>

struct LyricLine { int ms; std::wstring text; }; // when the line starts, from the start of the song

// https://lrclib.net/api, or PERCH_LRCLIB_URL (tests only).
std::wstring Lyrics_BaseUrl();
// Looks the song up. Returns 200 with `lines` sorted by time, 404 when there are no synced lyrics for it,
// or 0 when the service can't be reached.
int Lyrics_Fetch(const std::wstring& title, const std::wstring& artist, int seconds, std::vector<LyricLine>& lines);
// "[01:02.50] text" lines (LRC) -> sorted lines; tags like [ar:...] and lines with no time are skipped.
std::vector<LyricLine> Lyrics_Parse(const std::wstring& lrc);
