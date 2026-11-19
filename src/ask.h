// Ask Perch: talks to Google's Gemini API with the user's own key. Blocking calls, made from a worker
// thread. Nothing here draws; notch.cpp shows the question and the answer.
#pragma once
#include <string>
#include <vector>

struct AskTurn { bool user; std::wstring text; };

// The API's address: https://generativelanguage.googleapis.com/v1beta, or PERCH_GEMINI_URL (tests only).
std::wstring Ask_BaseUrl();

// Sends the conversation (oldest first, the last turn is the question) to `model`. Returns the HTTP status
// (0: couldn't reach it). On 200, `answer` is the reply as plain text; otherwise `error` says what went wrong.
int Ask_Generate(const std::wstring& key, const std::wstring& model, const std::vector<AskTurn>& turns,
                 std::wstring& answer, std::wstring& error);

// The models this key may use to generate text ("gemini-..." names, newest-looking first). Returns the status.
int Ask_ListModels(const std::wstring& key, std::vector<std::wstring>& models, std::wstring& error);
