// Updates from GitHub (see update.h), through net.cpp.
#include "update.h"
#include "net.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <vector>

static const wchar_t* kAgent = L"User-Agent: Perch-updater\r\nAccept: application/vnd.github+json\r\n";

std::wstring Update_ApiUrl() {
    wchar_t b[512];
    DWORD n = GetEnvironmentVariableW(L"PERCH_UPDATE_URL", b, 512);
    if (n && n < 512) return b;
    return L"https://api.github.com/repos/Karthikjl/perch/releases/latest";
}

static std::vector<int> Parts(const std::wstring& v) {
    std::vector<int> p;
    size_t i = v.size() && (v[0] == L'v' || v[0] == L'V') ? 1 : 0;
    while (i < v.size()) {
        int n = 0;
        bool digits = false;
        while (i < v.size() && iswdigit(v[i])) { n = n * 10 + (v[i] - L'0'); i++; digits = true; }
        if (!digits) break;
        p.push_back(n);
        if (i < v.size() && v[i] == L'.') i++;
        else break;
    }
    return p;
}

bool Update_Newer(const std::wstring& a, const std::wstring& b) {
    std::vector<int> x = Parts(a), y = Parts(b);
    if (x.empty()) return false;
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
}

int Update_Check(UpdateInfo& out) {
    std::string reply;
    int status = Net_Request(L"GET", Update_ApiUrl(), kAgent, "", reply);
    if (status != 200) return status;
    std::vector<std::wstring> tag = Net_JsonValues(reply, "tag_name");
    if (tag.empty()) return 204;
    out.version = tag[0].size() && (tag[0][0] == L'v' || tag[0][0] == L'V') ? tag[0].substr(1) : tag[0];
    out.url.clear();
    for (const std::wstring& u : Net_JsonValues(reply, "browser_download_url")) {
        size_t n = u.size();
        if (n >= 10 && _wcsicmp(u.c_str() + n - 10, L"/Perch.exe") == 0 && (u.compare(0, 8, L"https://") == 0 || u.compare(0, 17, L"http://127.0.0.1:") == 0)) { out.url = u; break; }
    }
    return 200;
}

bool Update_Download(const std::wstring& url, const std::wstring& path) {
    std::string body;
    if (Net_Request(L"GET", url, L"User-Agent: Perch-updater\r\n", "", body) != 200) return false;
    // A Windows program: "MZ", then a "PE" header where it says, and a sensible size.
    if (body.size() < 64 * 1024 || body[0] != 'M' || body[1] != 'Z') return false;
    DWORD pe = *(const DWORD*)(body.data() + 0x3C);
    if (pe + 4 > body.size() || body.compare(pe, 4, std::string("PE\0\0", 4)) != 0) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(f, body.data(), (DWORD)body.size(), &w, nullptr) && w == body.size();
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}
