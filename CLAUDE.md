# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes, from [andrej-karpathy-skills](https://github.com/multica-ai/andrej-karpathy-skills) (MIT).

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

---

## Perch project notes

Native C++ (Win32 + Direct2D) notch app for Windows. Code is in `src/`, the logo and icon in `assets/`, tests in `tests/`. See README.md for features.

**Naming:** the app is Perch: `Perch.exe`, window class `PerchWindow`, data in `%APPDATA%\Perch` (tests: `%PERCH_DATA_DIR%`), `Perch` under `HKCU\...\Run`. "Notch" in the code (`notch.cpp`, `[Notch]` in settings.ini, test helper names) means the notch itself.

**Build:** `build.bat` (MinGW from winget, else MSVC `cl` on PATH) builds `build\Perch.exe`. Quit the running copy first (right-click → Quit, or `Stop-Process -Name Perch`), since Windows locks a running `.exe`.

**Test:** `powershell -STA -ExecutionPolicy Bypass -File tests\run_all.ps1` builds, runs every `tests\test_*.ps1`, then starts a fresh copy. Tests move and click the real mouse and change volume, brightness and mute (then restore them), so run them only when the user agrees. A full screen app or another media player on the PC makes some suites fail. A single suite: `powershell -STA -ExecutionPolicy Bypass -File tests\test_<name>.ps1`.

**Workflow per feature:** add the feature → build → test. New features get their own `tests\test_*.ps1` and an entry in `run_all.ps1`.

**Performance budget:** collapsed idle < 13 MB in Task Manager and 0% CPU. Stay event-driven; no polling timers while collapsed.
