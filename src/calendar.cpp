// Calendar: iCal download and parsing. See calendar.h.
#include "calendar.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

// ---- days ----
int Cal_DayNumber(int y, int m, int d) { // days since 1970-01-01 (proleptic Gregorian)
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void Cal_Date(int z, int& y, int& m, int& d) {
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = yoe + era * 400 + (m <= 2);
}
int Cal_Weekday(int day) { return ((day % 7) + 11) % 7; } // 1970-01-01 was a Thursday
int Cal_Today() { SYSTEMTIME st; GetLocalTime(&st); return Cal_DayNumber(st.wYear, st.wMonth, st.wDay); }
static int DaysInMonth(int y, int m) { return Cal_DayNumber(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - Cal_DayNumber(y, m, 1); }

namespace {

struct When { int day = 0, minute = -1; bool ok = false; }; // minute -1: a date (all day)

// "20261119", "20261119T093000" (local / floating / TZID: taken as local time), "20261119T093000Z" (UTC)
When ParseWhen(const std::string& v) {
    When w;
    if (v.size() < 8) return w;
    for (int i = 0; i < 8; i++) if (v[i] < '0' || v[i] > '9') return w;
    int y = atoi(v.substr(0, 4).c_str()), mo = atoi(v.substr(4, 2).c_str()), d = atoi(v.substr(6, 2).c_str());
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return w;
    w.ok = true;
    w.day = Cal_DayNumber(y, mo, d);
    if (v.size() >= 13 && v[8] == 'T') {
        int h = atoi(v.substr(9, 2).c_str()), mi = atoi(v.substr(11, 2).c_str());
        if (v.size() >= 16 && v[15] == 'Z') { // UTC -> local
            SYSTEMTIME u = {}, l = {};
            u.wYear = (WORD)y; u.wMonth = (WORD)mo; u.wDay = (WORD)d; u.wHour = (WORD)h; u.wMinute = (WORD)mi;
            if (SystemTimeToTzSpecificLocalTime(nullptr, &u, &l)) { w.day = Cal_DayNumber(l.wYear, l.wMonth, l.wDay); h = l.wHour; mi = l.wMinute; }
        }
        w.minute = h * 60 + mi;
    }
    return w;
}

int ParseDurationMin(const std::string& v) { // "PT1H30M", "P1D"
    int total = 0, num = 0;
    bool neg = !v.empty() && v[0] == '-';
    for (char c : v) {
        if (c >= '0' && c <= '9') num = num * 10 + (c - '0');
        else {
            if (c == 'W') total += num * 7 * 1440;
            else if (c == 'D') total += num * 1440;
            else if (c == 'H') total += num * 60;
            else if (c == 'M') total += num;
            num = 0;
        }
    }
    return neg ? -total : total;
}

std::wstring Utf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Unescape(const std::string& v) { // iCal text: \n \, \; \\.
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char c = v[++i];
            o += (c == 'n' || c == 'N') ? ' ' : c;
        } else o += v[i];
    }
    return o;
}

struct ByDay { int n; int wd; }; // n = 0 every, 2 = second, -1 = last
struct Rule {
    int freq = 0; // 0 none, 1 daily, 2 weekly, 3 monthly, 4 yearly
    int interval = 1, count = 0, untilDay = 0x7fffffff;
    std::vector<ByDay> byDay;
    std::vector<int> byMonthDay;
};

int WeekdayCode(const std::string& s) {
    static const char* k[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int i = 0; i < 7; i++) if (s == k[i]) return i;
    return -1;
}

Rule ParseRule(const std::string& v) {
    Rule r;
    size_t p = 0;
    while (p < v.size()) {
        size_t semi = v.find(';', p);
        std::string part = v.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        p = semi == std::string::npos ? v.size() : semi + 1;
        size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string k = part.substr(0, eq), val = part.substr(eq + 1);
        if (k == "FREQ") r.freq = val == "DAILY" ? 1 : val == "WEEKLY" ? 2 : val == "MONTHLY" ? 3 : val == "YEARLY" ? 4 : -1;
        else if (k == "INTERVAL") r.interval = std::max(1, atoi(val.c_str()));
        else if (k == "COUNT") r.count = atoi(val.c_str());
        else if (k == "UNTIL") { When u = ParseWhen(val); if (u.ok) r.untilDay = u.day; }
        else if (k == "BYDAY" || k == "BYMONTHDAY") {
            size_t q = 0;
            while (q <= val.size()) {
                size_t c = val.find(',', q);
                std::string t = val.substr(q, c == std::string::npos ? std::string::npos : c - q);
                q = c == std::string::npos ? val.size() + 1 : c + 1;
                if (t.empty()) continue;
                if (k == "BYMONTHDAY") { r.byMonthDay.push_back(atoi(t.c_str())); continue; }
                int wd = t.size() >= 2 ? WeekdayCode(t.substr(t.size() - 2)) : -1;
                if (wd >= 0) r.byDay.push_back({atoi(t.substr(0, t.size() - 2).c_str()), wd});
            }
        }
    }
    return r;
}

struct VEvent {
    std::string uid, summary;
    When start, end, recurrenceId;
    int durationMin = -1;
    bool cancelled = false, hasRule = false;
    Rule rule;
    std::vector<int> exDays;
};

// Days of month (y, m) the rule picks, in order.
void MonthCandidates(int y, int m, const Rule& r, int startDom, std::vector<int>& days) {
    days.clear();
    int first = Cal_DayNumber(y, m, 1), n = DaysInMonth(y, m);
    if (!r.byMonthDay.empty()) {
        for (int md : r.byMonthDay) { int d = md > 0 ? md : n + md + 1; if (d >= 1 && d <= n) days.push_back(first + d - 1); }
    } else if (!r.byDay.empty()) {
        for (const ByDay& b : r.byDay) {
            std::vector<int> all;
            for (int d = 0; d < n; d++) if (Cal_Weekday(first + d) == b.wd) all.push_back(first + d);
            if (b.n == 0) days.insert(days.end(), all.begin(), all.end());
            else if (b.n > 0 && b.n <= (int)all.size()) days.push_back(all[b.n - 1]);
            else if (b.n < 0 && -b.n <= (int)all.size()) days.push_back(all[all.size() + b.n]);
        }
    } else if (startDom <= n) days.push_back(first + startDom - 1);
    std::sort(days.begin(), days.end());
    days.erase(std::unique(days.begin(), days.end()), days.end());
}

// Every day the event happens on, from its start up to toDay (and the rule's end), at most a few thousand.
void Occurrences(const VEvent& e, int toDay, std::vector<int>& out) {
    int s = e.start.day;
    if (!e.hasRule || e.rule.freq <= 0) { out.push_back(s); return; }
    const Rule& r = e.rule;
    int last = std::min(toDay, r.untilDay), made = 0;
    auto add = [&](int d) -> bool { // false: stop
        if (d < s) return true;
        if (d > last || (r.count && made >= r.count)) return false;
        made++;
        out.push_back(d);
        return true;
    };
    int y, m, dom;
    Cal_Date(s, y, m, dom);
    std::vector<int> cand;
    for (int k = 0; k < 5000; k++) {
        if (r.freq == 1) {
            if (!add(s + k * r.interval)) return;
        } else if (r.freq == 2) {
            int weekStart = s - (Cal_Weekday(s) + 6) % 7 + k * 7 * r.interval; // weeks start on Monday (iCal default)
            cand.clear();
            if (r.byDay.empty()) cand.push_back(weekStart + (Cal_Weekday(s) + 6) % 7);
            for (const ByDay& b : r.byDay) cand.push_back(weekStart + (b.wd + 6) % 7);
            std::sort(cand.begin(), cand.end());
            if (weekStart > last) return;
            for (int d : cand) if (!add(d)) return;
        } else if (r.freq == 3) {
            int mm = m - 1 + k * r.interval;
            int yy = y + mm / 12;
            mm = mm % 12 + 1;
            if (Cal_DayNumber(yy, mm, 1) > last) return;
            MonthCandidates(yy, mm, r, dom, cand);
            for (int d : cand) if (!add(d)) return;
        } else if (r.freq == 4) {
            int yy = y + k * r.interval;
            if (Cal_DayNumber(yy, 1, 1) > last) return;
            if (dom <= DaysInMonth(yy, m) && !add(Cal_DayNumber(yy, m, dom))) return;
        } else return;
    }
}

} // namespace

bool Cal_Parse(const std::string& ics, int fromDay, int toDay, std::vector<CalEvent>& out) {
    out.clear();
    if (ics.find("BEGIN:VCALENDAR") == std::string::npos) return false;

    // Unfold: a line starting with a space or tab continues the one before.
    std::vector<std::string> lines;
    size_t p = 0;
    while (p < ics.size()) {
        size_t e = ics.find('\n', p);
        std::string l = ics.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? ics.size() : e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && (l[0] == ' ' || l[0] == '\t') && !lines.empty()) lines.back() += l.substr(1);
        else lines.push_back(l);
    }

    std::vector<VEvent> events;
    VEvent cur;
    bool in = false;
    int depth = 0; // inside VALARM etc.
    for (const std::string& l : lines) {
        if (l == "BEGIN:VEVENT") { cur = VEvent(); in = true; depth = 0; continue; }
        if (!in) continue;
        if (l == "END:VEVENT") { if (cur.start.ok) events.push_back(cur); in = false; continue; }
        if (l.compare(0, 6, "BEGIN:") == 0) { depth++; continue; }
        if (l.compare(0, 4, "END:") == 0) { depth--; continue; }
        if (depth > 0) continue;
        size_t colon = std::string::npos;
        bool quoted = false;
        for (size_t i = 0; i < l.size(); i++) {
            if (l[i] == '"') quoted = !quoted;
            else if (l[i] == ':' && !quoted) { colon = i; break; }
        }
        if (colon == std::string::npos) continue;
        std::string head = l.substr(0, colon), value = l.substr(colon + 1);
        std::string name = head.substr(0, head.find(';'));
        if (name == "DTSTART") cur.start = ParseWhen(value);
        else if (name == "DTEND") cur.end = ParseWhen(value);
        else if (name == "DURATION") cur.durationMin = ParseDurationMin(value);
        else if (name == "SUMMARY") cur.summary = Unescape(value);
        else if (name == "UID") cur.uid = value;
        else if (name == "RRULE") { cur.rule = ParseRule(value); cur.hasRule = true; }
        else if (name == "RECURRENCE-ID") cur.recurrenceId = ParseWhen(value);
        else if (name == "STATUS") cur.cancelled = value == "CANCELLED";
        else if (name == "EXDATE") {
            size_t q = 0;
            while (q <= value.size()) {
                size_t c = value.find(',', q);
                When w = ParseWhen(value.substr(q, c == std::string::npos ? std::string::npos : c - q));
                if (w.ok) cur.exDays.push_back(w.day);
                q = c == std::string::npos ? value.size() + 1 : c + 1;
            }
        }
    }

    // A changed or cancelled repeat comes as its own event with RECURRENCE-ID: it replaces that day of the series.
    std::map<std::string, std::vector<int>> moved;
    for (const VEvent& e : events) if (e.recurrenceId.ok) moved[e.uid].push_back(e.recurrenceId.day);

    std::vector<int> days;
    for (const VEvent& e : events) {
        if (e.cancelled) continue;
        bool allDay = e.start.minute < 0;
        int startMin = e.start.minute, endMin = -1, spanDays = 1;
        if (allDay) {
            if (e.end.ok && e.end.day > e.start.day) spanDays = e.end.day - e.start.day;
            else if (e.durationMin > 0) spanDays = std::max(1, e.durationMin / 1440);
        } else if (e.end.ok) {
            endMin = e.end.day == e.start.day ? e.end.minute : e.end.day > e.start.day ? 24 * 60 : -1;
        } else if (e.durationMin >= 0) endMin = std::min(24 * 60, startMin + e.durationMin);
        if (endMin == 24 * 60 && !allDay) endMin = 0; // ends at midnight: show "-00:00"
        days.clear();
        Occurrences(e, toDay, days);
        auto mv = e.recurrenceId.ok ? moved.end() : moved.find(e.uid);
        for (int d : days) {
            if (std::find(e.exDays.begin(), e.exDays.end(), d) != e.exDays.end()) continue;
            if (mv != moved.end() && std::find(mv->second.begin(), mv->second.end(), d) != mv->second.end()) continue;
            for (int k = 0; k < spanDays; k++) {
                int day = d + k;
                if (day < fromDay || day > toDay) continue;
                out.push_back({day, allDay ? -1 : startMin, allDay ? -1 : endMin, Utf8(e.summary.empty() ? "(No title)" : e.summary)});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const CalEvent& a, const CalEvent& b) {
        return a.day != b.day ? a.day < b.day : a.start != b.start ? a.start < b.start : a.title < b.title;
    });
    return true;
}

int Cal_Download(const std::wstring& urlIn, std::string& body) {
    body.clear();
    std::wstring url = urlIn;
    if (_wcsnicmp(url.c_str(), L"webcal://", 9) == 0) url = L"https://" + url.substr(9);
    wchar_t host[256] = L"", path[4096] = L"";
    URL_COMPONENTS uc = {sizeof(uc)};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 1;
    std::wstring target = path;
    if (url.find(L'?') != std::wstring::npos && target.find(L'?') == std::wstring::npos) target += url.substr(url.find(L'?'));
    int result = 1;
    HINTERNET s = WinHttpOpen(L"Perch", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"Perch", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    if (s) WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            char buf[16384];
            DWORD got = 0;
            result = 0;
            while (WinHttpReadData(r, buf, sizeof(buf), &got) && got) {
                body.append(buf, got);
                if (body.size() > 32u << 20) { result = 1; break; } // not a calendar this size
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return result;
}
