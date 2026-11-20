// Ask Perch: talks to Google's Gemini API with the user's own key. Blocking calls, made from a worker
// thread. Nothing here draws; notch.cpp shows the question and the answer.
#pragma once
#include <string>
#include <vector>

struct AskTurn { bool user; std::wstring text; };
// What the model asked Perch to do (its one tool): add a to-do task. start / end are minutes since midnight, or -1.
struct AskTask { bool set = false; std::wstring title; int start = -1, end = -1; bool daily = false; };

// The API's address: https://generativelanguage.googleapis.com/v1beta, or PERCH_GEMINI_URL (tests only).
std::wstring Ask_BaseUrl();

// Sends the conversation (oldest first, the last turn is the question) to `model`. Returns the HTTP status
// (0: couldn't reach it). On 200, `answer` is the reply as plain text; otherwise `error` says what went wrong.
// The model may also ask to add a to-do task (`task`); Perch does that and says so.
int Ask_Generate(const std::wstring& key, const std::wstring& model, const std::vector<AskTurn>& turns,
                 std::wstring& answer, AskTask& task, std::wstring& error);

// The models this key may use to generate text ("gemini-..." names, newest-looking first). Returns the status.
int Ask_ListModels(const std::wstring& key, std::vector<std::wstring>& models, std::wstring& error);
