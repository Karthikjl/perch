// Ask Perch's calls to the Gemini API (see ask.h), through net.cpp.
#include "ask.h"
#include "net.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>

namespace {

std::wstring KeyHeaders(const std::wstring& key) { return L"Content-Type: application/json\r\nx-goog-api-key: " + key + L"\r\n"; }

std::wstring ErrorText(int status, const std::string& reply) {
    if (status == 0) return L"Couldn't reach Google. Check your connection.";
    if (status == 400 || status == 401 || status == 403) {
        std::vector<std::wstring> m = Net_JsonValues(reply, "message");
        if (!m.empty() && (m[0].find(L"API key") != std::wstring::npos || status != 400)) return L"Google didn't accept the key. Check it in Settings > Ask Perch.";
        return m.empty() ? L"Google refused the question." : m[0];
    }
    if (status == 404) return L"That model isn't available to your key. Pick another in Settings > Ask Perch.";
    if (status == 429) return L"Too many questions for now. Try again in a minute.";
    std::vector<std::wstring> m = Net_JsonValues(reply, "message");
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

// "HH:MM" -> minutes since midnight, or -1.
static int Minutes(const std::wstring& t) {
    int h = -1, m = -1;
    return swscanf(t.c_str(), L"%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60 ? h * 60 + m : -1;
}

int Ask_Generate(const std::wstring& key, const std::wstring& model, const std::vector<AskTurn>& turns,
                 std::wstring& answer, AskTask& task, std::wstring& error) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t now[64];
    swprintf(now, 64, L"It is %02d:%02d now. ", st.wHour, st.wMinute);
    // One tool: add a task to Perch's to-do list. Nothing else can be done from here, and the model is told so,
    // so it never claims to have set a reminder, sent a message or opened something.
    std::string body = "{\"systemInstruction\":{\"parts\":[{\"text\":" +
                       Net_JsonString(std::wstring(L"You are Perch, a helpful assistant in a small panel at the top of a Windows screen. ") + now +
                                      L"Answer briefly and plainly in a few short paragraphs or a short list. No tables. "
                                      L"You can add a task to the user's Perch to-do list for today with the add_task tool (with an optional "
                                      L"start and end time, and daily if it should repeat every day); a task with a start time gets a reminder then. "
                                      L"That is the only action you can take. You cannot set alarms, calendar events or other reminders, "
                                      L"send messages, open apps or change anything else, so never say you did; say what the user can do instead.") +
                       "}]},\"tools\":[{\"functionDeclarations\":[{\"name\":\"add_task\","
                       "\"description\":\"Adds a task to the user's Perch to-do list for today.\","
                       "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
                       "\"title\":{\"type\":\"STRING\",\"description\":\"Short task title, at most 60 characters\"},"
                       "\"start\":{\"type\":\"STRING\",\"description\":\"Start time today, HH:MM in 24 hours (optional)\"},"
                       "\"end\":{\"type\":\"STRING\",\"description\":\"End time today, HH:MM in 24 hours (optional)\"},"
                       "\"daily\":{\"type\":\"BOOLEAN\",\"description\":\"Repeat every day (optional)\"}},"
                       "\"required\":[\"title\"]}}]}],\"contents\":[";
    for (size_t i = 0; i < turns.size(); i++) {
        if (i) body += ",";
        body += std::string("{\"role\":\"") + (turns[i].user ? "user" : "model") + "\",\"parts\":[{\"text\":" + Net_JsonString(turns[i].text) + "}]}";
    }
    body += "]}";
    std::string reply;
    int status = Net_Request(L"POST", Ask_BaseUrl() + L"/models/" + model + L":generateContent", KeyHeaders(key), body, reply);
    if (status != 200) { error = ErrorText(status, reply); return status; }
    std::wstring all;
    for (const std::wstring& t : Net_JsonValues(reply, "text")) all += t;
    answer = Plain(all);
    size_t call = reply.find("\"functionCall\"");
    if (call != std::string::npos) { // the model wants a task added
        std::string c = reply.substr(call);
        std::vector<std::wstring> name = Net_JsonValues(c, "name"), title = Net_JsonValues(c, "title"), start = Net_JsonValues(c, "start"), end = Net_JsonValues(c, "end");
        if (!name.empty() && name[0] == L"add_task" && !title.empty() && !title[0].empty()) {
            task.set = true;
            task.title = title[0].substr(0, 60);
            task.start = start.empty() ? -1 : Minutes(start[0]);
            task.end = end.empty() || task.start < 0 ? -1 : Minutes(end[0]);
            if (task.end >= 0 && task.end <= task.start) task.end = -1;
            size_t d = c.find("\"daily\"");
            task.daily = d != std::string::npos && c.find("true", d) != std::string::npos && c.find("true", d) < c.find_first_of(",}", d + 7);
        }
    }
    if (answer.empty() && !task.set) { error = L"Google sent an empty answer. Try asking another way."; return 204; }
    return 200;
}

int Ask_ListModels(const std::wstring& key, std::vector<std::wstring>& models, std::wstring& error) {
    std::string reply;
    int status = Net_Request(L"GET", Ask_BaseUrl() + L"/models?pageSize=200", KeyHeaders(key), "", reply);
    if (status != 200) { error = ErrorText(status, reply); return status; }
    models.clear();
    // Each model: "name": "models/gemini-...", then its "supportedGenerationMethods" list.
    for (size_t p = reply.find("\"name\""); p != std::string::npos;) {
        size_t next = reply.find("\"name\"", p + 1);
        std::string block = reply.substr(p, next == std::string::npos ? std::string::npos : next - p);
        p = next;
        std::vector<std::wstring> v = Net_JsonValues(block, "name");
        if (v.empty() || v[0].compare(0, 7, L"models/") != 0) continue;
        std::wstring name = v[0].substr(7);
        if (name.compare(0, 7, L"gemini-") == 0 && block.find("\"generateContent\"") != std::string::npos &&
            name.find(L"embedding") == std::wstring::npos && name.find(L"image") == std::wstring::npos &&
            name.find(L"tts") == std::wstring::npos && name.find(L"audio") == std::wstring::npos)
            models.push_back(name);
    }
    std::sort(models.begin(), models.end(), [](const std::wstring& a, const std::wstring& b) { return a > b; });
    models.erase(std::unique(models.begin(), models.end()), models.end());
    return 200;
}
