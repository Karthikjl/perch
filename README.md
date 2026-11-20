<p align="center">
  <img src="assets/perch.svg" width="96" height="96" alt="Perch logo">
</p>

<h1 align="center">Perch</h1>

<p align="center">
  A tiny, fast notch for the top of your Windows screen.<br>
  Hover it for music, a focus timer, water, your shelf of files and today's tasks.
</p>

<p align="center">
  <b>~650 KB</b> single .exe &nbsp;·&nbsp; <b>~0.2 MB</b> RAM when closed &nbsp;·&nbsp; <b>0% CPU</b> at rest &nbsp;·&nbsp; nothing to install
</p>

---

## Why Perch

Perch lives in the middle of your screen's top edge as a small black pill showing the time and your
battery. Move the mouse onto it and it springs open into a panel of pages; move away and it tucks
itself back. It never steals focus, clicks pass straight through around it, and it steps aside for
full screen games and videos.

It's written in native C++ (Win32 + Direct2D). There's no browser engine, no runtime and no
background polling: when the notch is closed, Perch is asleep until something actually happens.

## Features

| | |
|---|---|
| **The notch** | A pill with the time and a battery ring, joined to the top edge (Classic) or floating just below it. Sizes 100–150%, a *Minimal* tab when nothing is playing, optional frosted *Glass*. Opens and closes with a soft spring. |
| **Music** | Title, artist and cover art from any app in Windows' media controls (Spotify, browsers, Media Player…), a progress bar, previous / play-pause / next and mute. A mini cover and sound bars on the closed pill. |
| **Lyrics** | Turn on *Show lyrics* (Settings → Music and lyrics) and the Music page shows the line being sung and the next one, in time with the song, from LRCLIB, a free lyrics database. Off by default: turning it on sends each song's title, artist and length to LRCLIB. |
| **Pomodoro** | Four focus sessions with short breaks, then a long break (25 / 5 / 15 min, adjustable). A pop-up and a chime when each part ends; daily focus minutes are kept. |
| **To-do** | Today's tasks with optional times and daily repeats. Reminders when a task starts and when its time is up, with snooze. |
| **Water** | Tap + for each cup and watch today's total fill up toward your goal (1.5–3 L). Optional reminders spread across your active hours nudge you only when you're behind; *Drank* or *Later* right on the pop-up. A Water ring for the closed notch, too. |
| **Calendar** | Month and week views on the To-do page, from your own Google Calendar iCal link. Refreshes every 30 minutes. A pop-up before each timed event today (10 min before by default, or at the start, 5, 15 or 30 min, or off), with *Later* and *OK*. |
| **Shelf** | Drop files on the notch to keep them handy, then drag them out into any app later. Survives restarts. |
| **Clipboard history** | Turn it on and the Shelf gets a Clipboard tab with the text you copy, newest first: click one to copy it again. Copies that password managers mark as private are skipped. Optionally kept after a restart, encrypted for your Windows account. Off by default. |
| **Ask Perch** | Press **Ctrl + Alt + P** anywhere (or any shortcut you set in Settings), type a question, press Enter: the answer appears right in the notch (Esc closes, *Copy answer* copies it). Uses Google's Gemini with your own free key from aistudio.google.com, and the model you pick in Settings from the ones your key can use. The key is stored encrypted for your Windows account. |
| **Pop-ups** | Volume, brightness, output device, battery (charging, low, full), Caps Lock, input language, Wi-Fi and Bluetooth headsets. Each can be turned off. |
| **Mascot** | Leave the PC alone for 15 seconds and a little friend appears in the notch. It looks around, dozes off and wakes up when you're back. Seven styles: the Falcon, Ember, Mint leaf, Cyclops, Visor bot, Sunny and Moonlight. |
| **Updates** | Perch checks GitHub for a new release a minute after it starts and once a day (switch off in Settings → About). When there is one, a pop-up says so; *Install update* (right-click menu, tray menu or Settings → About) downloads the new `Perch.exe`, swaps it in and restarts. Nothing is installed without you choosing it. |
| **Settings** | A settings window (tray icon, right-click → *Settings…*, or the gear on the page bar). Everything is also in a commented `settings.ini` that applies the moment you save it. |

## Getting started

1. Download `Perch.exe` from the [Releases](../../releases) page.
2. Run it. The notch appears at the top of your main screen.
3. Hover to open, scroll to change pages, right-click for the menu.
4. Turn on *Start with Windows* from the tray icon or the right-click menu, if you like.

Requires Windows 10 (2004 or later) or Windows 11, x64. Perch updates itself from then on (see Updates above).

### Tips

- **Scroll** over the open notch, or use the ‹ › arrows, to flip pages. Hover the dots for page icons.
- **Shelf from the command line:** `Perch.exe --shelf "C:\path\file.pdf" "C:\path\photo.png"` adds files to the running copy.
- **settings.ini:** every option has a comment. Delete a line, or the whole file, to go back to the default.

## Privacy

Perch doesn't send anything anywhere unless you ask it to. It downloads your calendar only if you add a
calendar link, looks up song lyrics on LRCLIB only if you turn lyrics on, checks GitHub for new versions of Perch (switch off in Settings → About), and sends your Ask Perch questions to Google only if you add your own Gemini key (to the
model you choose, using that key). Everything it keeps stays in `%APPDATA%\Perch`:

| File | What's in it |
|---|---|
| `settings.ini` | Your settings |
| `shelf.txt` | Paths of the files on your shelf (the files aren't copied) |
| `tasks.txt` | Your to-do list |
| `calendar.ics` | The last calendar download |
| `focus.txt` | Pomodoro sessions and focus minutes per day |
| `water.txt` | Water per day, in ml |
| `ask.key` | Your Gemini key, only if you add one; encrypted for your Windows account |
| `clipboard.dat` | Clipboard history, only if you turn on *Keep after restart*; encrypted for your Windows account |

*Start with Windows* adds `Perch` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

## Building from source

You need a C++17 compiler: MinGW-w64 (recommended) or Visual Studio Build Tools.

```
winget install BrechtSanders.WinLibs.POSIX.UCRT
build.bat
```

The result is `build\Perch.exe`. Quit a running Perch first, since Windows locks a running `.exe`.
GitHub Actions builds it on every push, and pushing a tag such as `v0.1.0` publishes a release.

## Tests

Each feature has a PowerShell test in `tests/`. This builds Perch, runs them all and starts a fresh copy:

```
powershell -STA -ExecutionPolicy Bypass -File tests\run_all.ps1
```

The tests briefly move and click the mouse and change volume, brightness and mute, then put
everything back, so run them while you're not using the PC. They use a temporary data folder where
it matters, and skip rather than click into another app.

## Project layout

| Path | What it is |
|---|---|
| `src/notch.cpp` | The app: window, drawing, animation, pages, pop-ups, shelf, tray icon |
| `src/mascot.cpp` | The mascot's looks, shared by the notch and the settings previews |
| `src/media.cpp` | Now playing and media controls (Windows media session API) |
| `src/calendar.cpp` | Calendar download (WinHTTP) and the iCal reader |
| `src/settings.cpp` | `settings.ini`: defaults, reading, saving and watching for changes |
| `src/settingsui.cpp` | The settings window |
| `assets/` | The logo (`perch.svg`) and icon; `make_icon.ps1` rebuilds `perch.ico` |
| `tests/` | One `test_*.ps1` per feature, shared helpers in `lib.ps1` |
| `build.bat` | Build script (MinGW or MSVC); output goes to `build/` |

## Known limitations

- The brightness pop-up covers the built-in laptop display only.
- Wired headphones on a shared jack may not trigger the output-device pop-up.
- Pop-ups don't show while a full screen app is in front.
- Some settings say *Coming soon*: widgets and a few more are on the way.

## Contributing

Bug reports, ideas and pull requests are welcome. [CONTRIBUTING.md](CONTRIBUTING.md) has the build,
test and pull request commands.

```
git clone https://github.com/Karthikjl/perch.git
cd perch
build.bat
```

## License

[MIT](LICENSE)
