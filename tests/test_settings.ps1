# 1.5 Settings file and window: defaults with no file, live reload on save, clear messages
# for bad values, back to defaults when the file is deleted; the settings window laid out like
# (switches, cards, Pages order, Coming soon parts, Reset, close, Esc) and the tray icon. Uses its own temporary data folder
# (PERCH_DATA_DIR). Clicks and keys are posted to the settings window; the real pointer only
# waits in a screen corner during the slider drag, then goes back.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 1.5 Settings =='
if (-not ('SetProbe' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SetProbe {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out NotchRect r);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll", EntryPoint="GetClassLongPtrW")] public static extern IntPtr GetClassLongPtr(IntPtr h, int index);
    [StructLayout(LayoutKind.Sequential)] public struct NotifyIconId { public int cbSize; public IntPtr hWnd; public uint uID; public Guid guidItem; }
    [DllImport("shell32.dll")] static extern int Shell_NotifyIconGetRect(ref NotifyIconId id, out NotchRect r);
    public static bool TrayIconShown(IntPtr notch) { NotifyIconId id = new NotifyIconId(); id.cbSize = Marshal.SizeOf(id); id.hWnd = notch; id.uID = 1; NotchRect r; return Shell_NotifyIconGetRect(ref id, out r) == 0; }
    [StructLayout(LayoutKind.Sequential)] public struct NotchRect { public int Left, Top, Right, Bottom; }
}
'@
}

$work = Join-Path $env:TEMP "perch-settings-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
$SQ = @{ Width = 2; Height = 3; Round = 4; Loads = 6; Style = 7; Glass = 8 }
function Get-Setting([int]$q) { [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]$q, [IntPtr]::Zero) }
function Write-Ini([string[]]$lines) { Set-Content -LiteralPath $ini -Value $lines -Encoding ASCII }
function IniText { # '' while the notch is still writing it (locked); Wait-Until simply asks again
    try { if (Test-Path $ini) { Get-Content $ini -Raw -ErrorAction Stop } else { '' } } catch [System.IO.IOException] { '' }
}

& {
try {
    Start-Notch

    # --- no file: defaults ---
    Assert (-not (Test-Path $ini)) 'no settings file to start with'
    Assert ((Get-Setting $SQ.Width) -eq 150 -and (Get-Setting $SQ.Height) -eq 30 -and (Get-Setting $SQ.Round) -eq 14) 'pill defaults to 150 x 30, roundness 14'
    Assert ((Get-Setting $SQ.Style) -eq 0 -and (Get-Setting $SQ.Glass) -eq 0) 'Classic style, no Glass'

    # --- a save applies straight away ---
    Write-Ini '[Notch]', 'Width=240', 'Height=34', 'Roundness=4', 'Style=Floating', 'Glass=1'
    Assert (Wait-Until { (Get-Setting $SQ.Width) -eq 240 } 3000) 'saving the file applies it without a restart'
    Assert ((Get-Setting $SQ.Height) -eq 34 -and (Get-Setting $SQ.Round) -eq 4) 'height and roundness too'
    Assert ((Get-Setting $SQ.Style) -eq 1 -and (Get-Setting $SQ.Glass) -eq 1) 'style and Glass too'
    Assert ((Get-ToastText) -like 'Settings updated*') "a pop-up confirms it ($(Get-ToastText))"
    Start-Sleep -Milliseconds 1800

    # --- bad values are reported, not silently ignored ---
    Write-Ini '[Notch]', 'Width=wide'
    Assert (Wait-Until { (Get-ToastText) -like 'Settings problem | Width: "wide"*' } 3000) "a value that isn't a number is reported ($(Get-ToastText))"
    Assert ((Get-Setting $SQ.Width) -eq 150) '...and that setting keeps its default'
    Write-Ini '[Notch]', 'Width=999'
    Assert (Wait-Until { (Get-Setting $SQ.Width) -eq 300 } 3000) 'an out-of-range width is limited to 300'
    Assert ((Get-ToastText) -like '*Width: use 120-300*') "...and the allowed range is shown ($(Get-ToastText))"

    # --- other files in the folder don't trigger a reload ---
    $loads = Get-Setting $SQ.Loads
    'x' | Set-Content "$work\note.txt"
    & $Exe --shelf "$work\note.txt"
    Assert (Wait-Until { Test-Path "$work\shelf.txt" } 2000) 'the shelf list is saved in the same folder'
    Start-Sleep -Milliseconds 800
    Assert ((Get-Setting $SQ.Loads) -eq $loads) "a shelf change does not reload settings (loads: $loads -> $(Get-Setting $SQ.Loads))"

    # --- deleting the file goes back to defaults ---
    Remove-Item $ini
    Assert (Wait-Until { (Get-Setting $SQ.Width) -eq 150 } 3000) 'deleting settings.ini goes back to the defaults'

    # --- the settings window (sections on the left, cards on the right) ---
    $loads = Get-Setting $SQ.Loads
    Wait-Until { -not (Test-Flag 'Toast') } 4000 | Out-Null
    [NotchTest]::Command((Get-NotchWindow), $CMD.Settings)
    Assert (Wait-Until { [NotchTest]::FindByClass('PerchSettings') -ne [IntPtr]::Zero } 3000) 'right-click -> Settings... opens the settings window'
    $setWin = [NotchTest]::FindByClass('PerchSettings')
    $cr = New-Object SetProbe+NotchRect; [SetProbe]::GetClientRect($setWin, [ref]$cr) | Out-Null
    $k = $cr.Right / 654.4
    Assert ([math]::Abs($cr.Bottom / $k - 556) -lt 2) ("the window is 654 x 556 ({0:N0} x {1:N0})" -f ($cr.Right / $k), ($cr.Bottom / $k))
    # Control ids (settingsui.cpp); the window tells where each one is.
    $C = @{ Close = 1; Section = 2; Autostart = 3; Size = 4; Minimal = 5; Mascot = 6; Clock = 7; Sounds = 8; HideFs = 9; Ring = 10; PageUp = 11; PageDown = 12; PageToggle = 13;
            Style = 14; Glass = 15; Tint = 16; Open = 17; PageIcons = 18; Speed = 19; TStart = 20; TEnd = 21; Snooze = 22; Week = 23; View = 24;
            Focus = 28; Short = 29; Long = 30; ShelfEmpty = 31; Volume = 32; Reset = 40; PetStyle = 41; WRemind = 42; WGoal = 43; WCup = 44; WFrom = 45; WUntil = 46; KModel = 53; KHotkey = 54 }
    function Section([int]$s) { [int][NotchTest]::SendMessage($setWin, [NotchTest]::WM_APP + 1, [IntPtr]$s, [IntPtr]::Zero) }
    function Spot([int]$id, [int]$arg = 0) { [int64][NotchTest]::SendMessage($setWin, [NotchTest]::WM_APP + 2, [IntPtr]$id, [IntPtr]$arg) }
    function Tap([int]$id, [int]$arg = 0) {
        $p = Spot $id $arg
        if ($p -lt 0) { return $false }
        [SetProbe]::PostMessage($setWin, 0x201, [IntPtr]1, [IntPtr]$p) | Out-Null; [SetProbe]::PostMessage($setWin, 0x202, [IntPtr]0, [IntPtr]$p) | Out-Null
        Start-Sleep -Milliseconds 200; return $true
    }
    function KeyDown([int]$vk) { [SetProbe]::PostMessage($setWin, 0x100, [IntPtr]$vk, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 60 }
    Start-Sleep -Milliseconds 400
    [NotchTest]::Command((Get-NotchWindow), $CMD.Settings)
    Start-Sleep -Milliseconds 300
    Assert ([NotchTest]::FindByClass('PerchSettings') -eq $setWin) 'choosing it again reuses the open window'
    Assert ([SetProbe]::GetClassLongPtr($setWin, -34) -ne [IntPtr]::Zero -and [SetProbe]::GetClassLongPtr($setWin, -14) -ne [IntPtr]::Zero) 'the window has the app icon (taskbar)'
    Assert ((Get-Setting $SQ.Loads) -eq $loads -and -not (Test-Path $ini)) 'opening it changes nothing on disk'
    Assert ((Section -1) -eq 0) 'it opens on General'

    # General
    Assert (Tap $C.Minimal) 'General: Minimal notch switch found'
    Assert (Wait-Until { (Get-Setting 10) -eq 1 -and (IniText) -match '(?m)^Minimal=1' } 2000) '...it turns Minimal on and saves it'
    Assert ((IniText) -match '; Perch settings') '...in the commented file (comments kept)'
    Start-Sleep -Milliseconds 400 # past the file watcher's settle time
    Assert (-not (Test-Flag 'Toast')) 'no pop-up for changes made in the window'
    Tap $C.Minimal | Out-Null
    Tap $C.Size 2 | Out-Null
    Assert (Wait-Until { (Get-Setting 9) -eq 2 -and (IniText) -match '(?m)^Size=115%' } 2000) 'the Size buttons: 115%'
    Tap $C.Size 0 | Out-Null
    Assert (Tap $C.PetStyle 1) 'General: the mascot style cards are there'
    Assert (Wait-Until { (IniText) -match '(?m)^MascotStyle=Ember' } 2000) '...choosing Ember saves it'
    Tap $C.PetStyle 0 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^MascotStyle=Falcon' } 2000) '...and back to the Falcon'
    Assert (Wait-Until { (Get-Setting 9) -eq 0 } 2000) '...and back to Auto'
    Tap $C.HideFs | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^HideInFullScreen=0' } 2000) 'Step aside for full screen can be switched off'
    # Display: the ring cards
    Section 1 | Out-Null
    Tap $C.Ring 2 | Out-Null
    Assert (Wait-Until { (Get-Setting 12) -eq 2 -and (IniText) -match '(?m)^Ring=Tasks' } 2000) 'Display: the To-do card sets Ring=Tasks'
    Tap $C.Ring 1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Ring=Water' } 2000) '...the Water card sets Ring=Water'
    Tap $C.Ring 0 | Out-Null
    # Pages: hide Water, move Shelf up
    Section 2 | Out-Null
    Tap $C.PageToggle 4 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Hidden=.*water' } 2000) 'Pages: switching Water off hides it'
    Tap $C.PageUp 2 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Order=music,shelf,pomodoro,todo' } 2000) "...and the arrow moves Shelf up ($([regex]::Match((IniText), '(?m)^Order=.*').Value))"
    # Appearance: style cards and Glass
    Section 3 | Out-Null
    Tap $C.Style 1 | Out-Null
    Assert (Wait-Until { (Get-Setting $SQ.Style) -eq 1 -and (IniText) -match '(?m)^Style=Floating' } 2000) 'Appearance: the Floating card turns Floating on and saves it'
    Tap $C.Glass | Out-Null
    Assert (Wait-Until { (Get-Setting $SQ.Glass) -eq 1 -and (IniText) -match '(?m)^Glass=1' } 2000) 'the Glass switch turns Glass on and saves it'
    Tap $C.Tint 1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^GlassTint=50' } 2000) 'Glass tint + goes to 50%'
    Tap $C.Open 1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Open=Click' } 2000) 'Open the notch: Click'
    Tap $C.Open 0 | Out-Null
    # Pomodoro steppers, HUD switches, Shelf
    Section 8 | Out-Null
    Tap $C.Focus 1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Focus=30' } 2000) 'Pomodoro: + makes Focus 30 min'
    Section 11 | Out-Null
    Tap $C.Volume | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Volume=0' } 2000) 'HUD & popups: the Volume switch turns the volume pop-up off'
    Section 10 | Out-Null
    Tap $C.ShelfEmpty | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^StartEmpty=1' } 2000) 'Shelf: Empty shelf at start'
    Section 6 | Out-Null
    Assert ((Spot $C.KHotkey) -ge 0) 'Ask Perch: the section is live (shortcut switch)'
    Tap $C.KHotkey | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Hotkey=0' } 2000) 'Ask Perch: the shortcut can be switched off'
    Tap $C.KHotkey | Out-Null
    Section 9 | Out-Null
    Tap $C.WRemind | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Reminders=1' } 2000) 'Water: the reminders switch turns them on'
    Tap $C.WGoal 2 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Goal=2500' } 2000) 'Water: Daily goal 2.5 L'
    Tap $C.WCup 1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Cup=300' } 2000) 'Water: + makes the cup 300 ml'
    Tap $C.WFrom 1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^From=08:30' } 2000) 'Water: Active from 08:30'
    Tap $C.WUntil -1 | Out-Null
    Assert (Wait-Until { (IniText) -match '(?m)^Until=16:30' } 2000) 'Water: Active until 16:30'

    # hand edits while the window is open still apply
    $edited = (IniText) -replace '(?m)^Height=\d+', 'Height=36'
    for ($try = 0; $try -lt 20; $try++) { try { Set-Content $ini -Value $edited -Encoding ASCII -NoNewline -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 100 } } # Perch may still be saving the last switch
    Assert (Wait-Until { (Get-Setting $SQ.Height) -eq 36 } 3000) 'editing the file while the window is open still applies'

    Section 12 | Out-Null
    Tap $C.Reset | Out-Null
    Assert (Wait-Until { (Get-Setting $SQ.Width) -eq 150 -and (Get-Setting $SQ.Style) -eq 0 -and (Get-Setting $SQ.Glass) -eq 0 -and (Get-Setting $SQ.Height) -eq 30 } 2000) 'About > Reset to defaults restores every setting'
    Assert ((IniText) -match '(?m)^Width=150' -and (IniText) -match '(?m)^Order=music,pomodoro,water,shelf,todo') '...and saves them'
    Tap $C.Close | Out-Null
    Assert (Wait-Until { -not [SetProbe]::IsWindow($setWin) } 2000) 'the red dot closes the window'

    # tray icon and its menu commands
    Assert ([SetProbe]::TrayIconShown((Get-NotchWindow))) 'the tray icon is there'
    [NotchTest]::Command((Get-NotchWindow), 117) # About Perch
    Assert (Wait-Until { [NotchTest]::FindByClass('PerchSettings') -ne [IntPtr]::Zero } 3000) 'tray: About Perch opens the settings window...'
    $setWin = [NotchTest]::FindByClass('PerchSettings')
    Assert ((Section -1) -eq 12) '...on About'
    KeyDown 0x1B
    Assert (Wait-Until { -not [SetProbe]::IsWindow($setWin) } 2000) 'Esc closes it'
    [NotchTest]::Command((Get-NotchWindow), 132) # Notch size 115%
    Assert (Wait-Until { (Get-Setting 9) -eq 2 } 2000) 'tray: Notch size > 115%'
    [NotchTest]::Command((Get-NotchWindow), 130)
    [NotchTest]::Command((Get-NotchWindow), 116) # mascot off
    Assert (Wait-Until { (IniText) -match '(?m)^Mascot=0' } 2000) 'tray: Perch mascot switches off'
    Remove-Item $ini

    # --- the watcher (and the closed window) cost nothing while idle ---
    Wait-Until { -not (Test-Flag 'Toast') -and -not (Test-Flag 'Expanded') } 6000 | Out-Null
    Start-Sleep -Seconds 2
    $p = Get-Process Perch; $c1 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $p.Refresh()
    $cpu = $p.TotalProcessorTime.TotalMilliseconds - $c1
    Assert ($cpu -lt 20) ("idle CPU over 5 s with the file watcher: {0:N0} ms" -f $cpu)
    $ws = $p.WorkingSet64 / 1MB
    Assert ($ws -lt 13) ("idle working set after using the settings window: {0:N1} MB < 13 MB" -f $ws)
} finally {
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
}
Finish-Test 'settings'
