// Synced lyrics from LRCLIB (see lyrics.h).
#include "lyrics.h"
#include "net.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>

std::wstring Lyrics_BaseUrl() {
    wchar_t b[512];
    DWORD n = GetEnvironmentVariableW(L"PERCH_LRCLIB_URL", b, 512);
    if (n && n < 512) return b;
    return L"https://lrclib.net/api";
}

std::vector<LyricLine> Lyrics_Parse(const std::wstring& lrc) {
    std::vector<LyricLine> out;
    size_t p = 0;
    while (p < lrc.size()) {
        size_t e = lrc.find(L'\n', p);
        std::wstring line = lrc.substr(p, e == std::wstring::npos ? std::wstring::npos : e - p);
        p = e == std::wstring::npos ? lrc.size() : e + 1;
        std::vector<int> times;
        size_t i = 0;
        while (i < line.size() && line[i] == L'[') { // one or more [mm:ss.xx] in front of the text
            size_t close = line.find(L']', i);
            if (close == std::wstring::npos) break;
            int m = 0;
            double sec = 0;
            if (swscanf(line.substr(i + 1, close - i - 1).c_str(), L"%d:%lf", &m, &sec) == 2 && m >= 0 && sec >= 0) times.push_back((int)(m * 60000 + sec * 1000 + 0.5));
            i = close + 1;
        }
        std::wstring text = line.substr(std::min(i, line.size()));
        while (!text.empty() && (text.back() == L'\r' || text.back() == L' ')) text.pop_back();
        while (!text.empty() && text.front() == L' ') text.erase(text.begin());
        for (int t : times) out.push_back({t, text});
    }
    std::stable_sort(out.begin(), out.end(), [](const LyricLine& a, const LyricLine& b) { return a.ms < b.ms; });
    return out;
}

int Lyrics_Fetch(const std::wstring& title, const std::wstring& artist, int seconds, std::vector<LyricLine>& lines) {
    std::wstring q = L"track_name=" + Net_UrlEncode(title) + L"&artist_name=" + Net_UrlEncode(artist);
    std::string reply;
    int status = Net_Request(L"GET", Lyrics_BaseUrl() + L"/get?" + q + (seconds > 0 ? L"&duration=" + std::to_wstring(seconds) : L""),
                             L"User-Agent: Perch/0.1 (a notch for Windows)\r\n", "", reply);
    if (status == 0) return 0;
    std::vector<std::wstring> synced = status == 200 ? Net_JsonValues(reply, "syncedLyrics") : std::vector<std::wstring>{};
    if (synced.empty()) { // not an exact match (or no times): the first search result that has them
        reply.clear();
        status = Net_Request(L"GET", Lyrics_BaseUrl() + L"/search?" + q, L"User-Agent: Perch/0.1 (a notch for Windows)\r\n", "", reply);
        if (status == 0) return 0;
        if (status == 200) synced = Net_JsonValues(reply, "syncedLyrics");
    }
    for (const std::wstring& s : synced) {
        lines = Lyrics_Parse(s);
        if (!lines.empty()) return 200;
    }
    return 404;
}
