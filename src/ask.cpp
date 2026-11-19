// Ask Perch's calls to the Gemini API (see ask.h): WinHTTP, JSON written and read by hand.
#include "ask.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cwchar>

namespace {

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string JsonString(const std::wstring& w) { // a quoted JSON string
    std::string s = Utf8(w), o = "\"";
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

// Reads the JSON string that starts at s[i] (the opening quote); i ends just past the closing one.
std::wstring ReadString(const std::string& s, size_t& i) {
    std::wstring out;
    std::string run;
    auto flush = [&] { out += Wide(run); run.clear(); };
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

// Every string value of `"key": "..."` in the text, in order.
std::vector<std::wstring> Values(const std::string& s, const char* key) {
    std::vector<std::wstring> out;
    std::string pat = std::string("\"") + key + "\"";
    for (size_t p = s.find(pat); p != std::string::npos; p = s.find(pat, p + 1)) {
        size_t i = p + pat.size();
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t' || s[i] == ':')) i++;
        if (i < s.size() && s[i] == '"') out.push_back(ReadString(s, i));
    }
    return out;
}

// One HTTP request. Returns the status (0: no connection) and the body.
int Request(const wchar_t* verb, const std::wstring& url, const std::wstring& key, const std::string& body, std::string& reply) {
    URL_COMPONENTS u = {sizeof(u)};
    wchar_t host[256] = L"", path[2048] = L"";
    u.lpszHostName = host; u.dwHostNameLength = 256;
    u.lpszUrlPath = path; u.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return 0;
    int status = 0;
    HINTERNET s = WinHttpOpen(L"Perch", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, verb, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        WinHttpSetTimeouts(r, 10000, 10000, 20000, 60000); // an answer can take a while
        std::wstring headers = L"Content-Type: application/json\r\nx-goog-api-key: " + key + L"\r\n";
        if (WinHttpSendRequest(r, headers.c_str(), (DWORD)-1, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                               (DWORD)body.size(), (DWORD)body.size(), 0) &&
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

std::wstring ErrorText(int status, const std::string& reply) {
    if (status == 0) return L"Couldn't reach Google. Check your connection.";
    if (status == 400 || status == 401 || status == 403) {
        std::vector<std::wstring> m = Values(reply, "message");
        if (!m.empty() && (m[0].find(L"API key") != std::wstring::npos || status != 400)) return L"Google didn't accept the key. Check it in Settings > Ask Perch.";
        return m.empty() ? L"Google refused the question." : m[0];
    }
    if (status == 404) return L"That model isn't available to your key. Pick another in Settings > Ask Perch.";
    if (status == 429) return L"Too many questions for now. Try again in a minute.";
    std::vector<std::wstring> m = Values(reply, "message");
    return m.empty() ? L"Google couldn't answer right now." : m[0];
}

// Markdown to plain text: no ** or #, "* " bullets become "\x2022 ".
std::wstring Plain(std::wstring t) {
    std::wstring o;
    bool lineStart = true;
    for (size_t i = 0; i < t.size(); i++) {
        wchar_t c = t[i];
        if (c == L'*' && i + 1 < t.size() && t[i + 1] == L'*') { i++; continue; }
        if (lineStart) {
            size_t j = i;
            while (j < t.size() && t[j] == L'#') j++;
            if (j > i && j < t.size() && t[j] == L' ') { i = j; continue; }
            if ((c == L'*' || c == L'-') && i + 1 < t.size() && t[i + 1] == L' ') { o += L"\x2022"; lineStart = false; continue; }
        }
        if (c == L'`') continue;
        o += c;
        lineStart = c == L'\n';
    }
    while (!o.empty() && iswspace(o.back())) o.pop_back();
    return o;
}

} // namespace

std::wstring Ask_BaseUrl() {
    wchar_t b[512];
    DWORD n = GetEnvironmentVariableW(L"PERCH_GEMINI_URL", b, 512);
    if (n && n < 512) return b;
    return L"https://generativelanguage.googleapis.com/v1beta";
}

int Ask_Generate(const std::wstring& key, const std::wstring& model, const std::vector<AskTurn>& turns,
                 std::wstring& answer, std::wstring& error) {
    std::string body = "{\"systemInstruction\":{\"parts\":[{\"text\":" +
                       JsonString(L"You are Perch, a helpful assistant in a small panel at the top of a Windows screen. "
                                  L"Answer briefly and plainly in a few short paragraphs or a short list. No tables.") +
                       "}]},\"contents\":[";
    for (size_t i = 0; i < turns.size(); i++) {
        if (i) body += ",";
        body += std::string("{\"role\":\"") + (turns[i].user ? "user" : "model") + "\",\"parts\":[{\"text\":" + JsonString(turns[i].text) + "}]}";
    }
    body += "]}";
    std::string reply;
    int status = Request(L"POST", Ask_BaseUrl() + L"/models/" + model + L":generateContent", key, body, reply);
    if (status != 200) { error = ErrorText(status, reply); return status; }
    std::wstring all;
    for (const std::wstring& t : Values(reply, "text")) all += t;
    answer = Plain(all);
    if (answer.empty()) { error = L"Google sent an empty answer. Try asking another way."; return 204; }
    return 200;
}

int Ask_ListModels(const std::wstring& key, std::vector<std::wstring>& models, std::wstring& error) {
    std::string reply;
    int status = Request(L"GET", Ask_BaseUrl() + L"/models?pageSize=200", key, "", reply);
    if (status != 200) { error = ErrorText(status, reply); return status; }
    models.clear();
    // Each model: "name": "models/gemini-...", then its "supportedGenerationMethods" list.
    for (size_t p = reply.find("\"name\""); p != std::string::npos; p = reply.find("\"name\"", p + 1)) {
        size_t i = reply.find('"', p + 6);
        if (i == std::string::npos) break;
        std::wstring name = ReadString(reply, i);
        if (name.compare(0, 7, L"models/") != 0) continue;
        name = name.substr(7);
        size_t next = reply.find("\"name\"", i);
        std::string block = reply.substr(i, next == std::string::npos ? std::string::npos : next - i);
        if (name.compare(0, 7, L"gemini-") == 0 && block.find("\"generateContent\"") != std::string::npos &&
            name.find(L"embedding") == std::wstring::npos && name.find(L"image") == std::wstring::npos &&
            name.find(L"tts") == std::wstring::npos && name.find(L"audio") == std::wstring::npos)
            models.push_back(name);
    }
    std::sort(models.begin(), models.end(), [](const std::wstring& a, const std::wstring& b) { return a > b; });
    models.erase(std::unique(models.begin(), models.end()), models.end());
    return 200;
}
