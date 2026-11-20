// Web helpers (see net.h).
#include "net.h"
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>

std::string Net_Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring Net_Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Net_JsonString(const std::wstring& w) {
    std::string s = Net_Utf8(w), o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

std::wstring Net_UrlEncode(const std::wstring& w) {
    std::string s = Net_Utf8(w);
    std::wstring o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (wchar_t)c;
        else { wchar_t b[4]; swprintf(b, 4, L"%%%02X", c); o += b; }
    }
    return o;
}

// The JSON string that starts at s[i] (its opening quote); i ends just past the closing one.
static std::wstring ReadString(const std::string& s, size_t& i) {
    std::wstring out;
    std::string run;
    auto flush = [&] { out += Net_Wide(run); run.clear(); };
    for (i++; i < s.size() && s[i] != '"'; i++) {
        if (s[i] != '\\') { run += s[i]; continue; }
        if (++i >= s.size()) break;
        char e = s[i];
        if (e == 'n') run += '\n';
        else if (e == 't') run += '\t';
        else if (e == 'r') {}
        else if (e == 'u' && i + 4 < s.size()) {
            flush();
            out += (wchar_t)strtol(s.substr(i + 1, 4).c_str(), nullptr, 16); // surrogate pairs come as two
            i += 4;
        } else run += e; // \" \\ \/
    }
    flush();
    if (i < s.size()) i++;
    return out;
}

std::vector<std::wstring> Net_JsonValues(const std::string& s, const char* key) {
    std::vector<std::wstring> out;
    std::string pat = std::string("\"") + key + "\"";
    for (size_t p = s.find(pat); p != std::string::npos; p = s.find(pat, p + 1)) {
        size_t i = p + pat.size();
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t' || s[i] == ':')) i++;
        if (i < s.size() && s[i] == '"') out.push_back(ReadString(s, i));
    }
    return out;
}

int Net_Request(const wchar_t* verb, const std::wstring& url, const std::wstring& headers, const std::string& body, std::string& reply) {
    URL_COMPONENTS u = {sizeof(u)};
    wchar_t host[256] = L"", path[4096] = L"";
    u.lpszHostName = host; u.dwHostNameLength = 256;
    u.lpszUrlPath = path; u.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return 0;
    std::wstring target = path; // path and query
    int status = 0;
    HINTERNET s = WinHttpOpen(L"Perch", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, verb, target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        WinHttpSetTimeouts(r, 10000, 10000, 20000, 60000);
        if (WinHttpSendRequest(r, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), (DWORD)-1,
                               body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            char buf[8192];
            DWORD got = 0;
            while (WinHttpReadData(r, buf, sizeof buf, &got) && got > 0 && reply.size() < (4u << 20)) reply.append(buf, got);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return status;
}
