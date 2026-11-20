# Contributing to Perch

Thanks for helping. Bug reports, ideas and pull requests are all welcome.

## Get the code

```
git clone https://github.com/Karthikjl/perch.git
cd perch
```

To send changes, fork the repo on GitHub first and clone your fork instead, then add this repo as
`upstream` so you can stay up to date:

```
git remote add upstream https://github.com/Karthikjl/perch.git
git fetch upstream
git rebase upstream/main
```

## Build

You need Windows 10 (2004+) or 11 and a C++17 compiler. MinGW-w64 is the easiest:

```
winget install BrechtSanders.WinLibs.POSIX.UCRT
build.bat
```

`build.bat` also works with Visual Studio Build Tools (run it from a Developer Command Prompt so `cl`
is on PATH). The output is `build\Perch.exe`.

Windows locks a running `.exe`, so quit Perch before rebuilding:

```
powershell -Command "Stop-Process -Name Perch -ErrorAction SilentlyContinue"
build.bat
build\Perch.exe
```

To try Perch without touching your own settings and shelf, point it at a scratch folder:

```
set PERCH_DATA_DIR=%TEMP%\perch-dev
build\Perch.exe
```

### The Rust part (perchctl)

`rust/` holds the cross-platform core and `perchctl`, the command line and MCP server. Install Rust from
[rustup.rs](https://rustup.rs) (on Windows the GNU toolchain works with MinGW), then:

```
cd rust
cargo test
cargo build --release
```

The result is `rust/target/release/perchctl` (`.exe` on Windows). Try the MCP server by hand:

```
echo {"jsonrpc":"2.0","id":1,"method":"tools/list"} | rust\target\release\perchctl mcp
```

## Test

Every feature has a PowerShell test in `tests/`. Run them all (this builds first and starts a fresh
copy at the end):

```
powershell -STA -ExecutionPolicy Bypass -File tests\run_all.ps1
```

Or a single suite:

```
powershell -STA -ExecutionPolicy Bypass -File tests\test_core.ps1
```

The tests move and click the real mouse and change volume, brightness and mute (then put them back).
Run them when you're not using the PC, with no full screen app or other media playing.

## Make a change

```
git checkout -b my-change
# edit, build, test
git add -A
git commit -m "Short summary of what changed"
git push -u origin my-change
```

Then open a pull request on GitHub against `main`. In the description, say what changed, why, and how
you tested it (which suites you ran, and a screenshot for anything visual).

## Guidelines

- **Stay light.** With the notch closed Perch should use under 13 MB in Task Manager and 0% CPU.
  Everything is event-driven: no polling timers while the notch is closed.
- **Keep changes focused.** One feature or fix per pull request; match the style of the code around it.
- **Add a test.** A new feature gets its own `tests\test_<name>.ps1` and an entry in `tests\run_all.ps1`.
- **Settings** go in `src/settings.cpp` (with a comment in the default `settings.ini`) and, if they
  belong in the window, in `src/settingsui.cpp`.

## Releases

Pushing a version tag builds `Perch.exe` on GitHub Actions and publishes a release, using the tag's
message as the release notes:

```
git tag -a v0.2.0 -m "Perch 0.2.0"
git push origin v0.2.0
```

## Reporting a bug

Open an issue with your Windows version, display scaling, what you did, what you expected and what
happened. A screenshot or a copy of your `%APPDATA%\Perch\settings.ini` helps a lot.

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
