// Small web helpers shared by Ask Perch (ask.cpp) and lyrics (lyrics.cpp): one blocking WinHTTP request,
// UTF-8 conversion, and reading string values out of a JSON reply. Called from worker threads only.
#pragma once
#include <string>
#include <vector>

std::string Net_Utf8(const std::wstring& w);
std::wstring Net_Wide(const std::string& s);
std::string Net_JsonString(const std::wstring& w);  // a quoted, escaped JSON string
std::wstring Net_UrlEncode(const std::wstring& w);   // for a query value
// Every string value of "key": "..." in the JSON text, in order.
std::vector<std::wstring> Net_JsonValues(const std::string& json, const char* key);
// One request; `headers` are extra lines ending in \r\n. Returns the HTTP status (0: no connection).
int Net_Request(const wchar_t* verb, const std::wstring& url, const std::wstring& headers, const std::string& body, std::string& reply);
