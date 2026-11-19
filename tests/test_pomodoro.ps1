# Pomodoro page : Ready 25:00, Start / Pause / Resume, the round of 4 focus sessions
# with short breaks and a long break, the pop-up wording, the chime, focus.txt stats, Reset,
# durations from settings.ini. Phases are ended early with a test message instead of waiting
# 25 minutes. One real click on the Start button (only if the notch is under the pointer); the
# pointer goes back afterwards. Uses its own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Pomodoro =='
if (-not ('PomoProbe' -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Runtime.InteropServices;
public static class PomoProbe {
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(System.Drawing.Point p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, UIntPtr e);
}
'@
}
$work = Join-Path $env:TEMP "perch-pomodoro-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
$CMDP = @{ Toggle = 110; Reset = 111 }
function Pomo([int]$wp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 26, [IntPtr]$wp, [IntPtr]::Zero) }
function State { $v = Pomo; [pscustomobject]@{ State = $v -band 3; Phase = ($v -shr 2) -band 3; Session = ($v -shr 4) -band 15; Left = $v -shr 8 } }
function Cmd([int]$c) { [NotchTest]::Command((Get-NotchWindow), $c); Start-Sleep -Milliseconds 100 }
function EndPhase { Pomo 1 | Out-Null; Start-Sleep -Milliseconds 100 }
function Wait-Quiet { Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null; Start-Sleep -Milliseconds 150 }

$cursor = Save-Cursor
& {
try {
    Start-Notch

    # --- Ready ---
    $s = State
    Assert ($s.State -eq 0 -and $s.Phase -eq 0 -and $s.Session -eq 0 -and $s.Left -eq 1500) "Ready: focus 1 of 4, 25:00 ($($s.Left) s)"

    # --- one real click on Start ---
    [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]1, [IntPtr]::Zero) | Out-Null
    if (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) {
        Start-Sleep -Milliseconds 400
        Write-Host "  screenshot: $(Save-TopShot 'pomodoro-ready')"
        $h = Get-NotchWindow; $r = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect($h, [ref]$r) | Out-Null
        $v = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 20, [IntPtr]40, [IntPtr]::Zero)
        $x = $r.Left + [int]($v -band 0xFFFF); $y = $r.Top + [int](($v -shr 16) -band 0xFFFF)
        [NotchTest]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 350
        if ([PomoProbe]::GetAncestor([PomoProbe]::WindowFromPoint((New-Object System.Drawing.Point $x, $y)), 2) -eq $h) {
            [PomoProbe]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [PomoProbe]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
            Assert (Wait-Until { (State).State -eq 1 } 1500) 'clicking Start starts the focus session'
            Start-Sleep -Milliseconds 1300
            Write-Host "  screenshot: $(Save-TopShot 'pomodoro-running')"
        } else { Write-Host '  SKIP  something else is over the Start button' -ForegroundColor Yellow; Cmd $CMDP.Toggle }
    } else { Write-Host '  SKIP  the notch did not open; starting without a click' -ForegroundColor Yellow; Cmd $CMDP.Toggle }
    Restore-Cursor $cursor

    # --- running, pause, resume ---
    $s = State
    Assert ($s.State -eq 1 -and $s.Left -le 1500 -and $s.Left -ge 1490) "running: counting down ($($s.Left) s left)"
    Start-Sleep -Milliseconds 1200
    Assert ((State).Left -lt $s.Left) 'the time left goes down'
    Cmd $CMDP.Toggle
    $p = State
    Assert ($p.State -eq 2) 'Pause pauses it'
    Start-Sleep -Milliseconds 1500
    Assert ((State).Left -eq $p.Left) "...and the time stops ($($p.Left) s)"
    Cmd $CMDP.Toggle
    Assert ((State).State -eq 1) 'Resume carries on'

    # --- the round: focus -> short break -> focus ... -> long break -> Ready ---
    $chimes = Pomo 2
    EndPhase
    $s = State
    Assert ($s.State -eq 1 -and $s.Phase -eq 1 -and $s.Session -eq 0 -and $s.Left -le 300) "focus done: the 5 min break starts by itself ($($s.Left) s)"
    Assert ((Get-ToastText) -eq 'Focus finished | Take a 5 min break') "pop-up: $(Get-ToastText)"
    Assert ((Pomo 2) -eq $chimes + 1) 'a chime plays'
    Assert ((Pomo 3) -eq 25) "focus.txt counts 25 focus minutes today ($(Pomo 3))"
    Wait-Quiet
    EndPhase
    $s = State
    Assert ($s.Phase -eq 0 -and $s.Session -eq 1 -and $s.State -eq 1) 'break over: focus 2 of 4 starts'
    Assert ((Get-ToastText) -eq 'Break finished | Focus session 2 of 4') "pop-up: $(Get-ToastText)"
    foreach ($i in 1..5) { EndPhase } # focus 2, break, focus 3, break, focus 4
    $s = State
    Assert ($s.Phase -eq 2 -and $s.State -eq 1 -and $s.Left -le 900 -and $s.Left -ge 890) "after focus 4: the 15 min long break ($($s.Left) s)"
    Assert ((Get-ToastText) -eq 'Round finished | Enjoy a 15 min break') "pop-up: $(Get-ToastText)"
    Assert ((Pomo 3) -eq 100) "four sessions = 100 focus minutes today ($(Pomo 3))"
    $line = (Get-Content "$work\focus.txt" | Select-Object -Last 1)
    Assert ($line -match '^\d{8} 4 100$') "focus.txt: '$line'"
    EndPhase
    $s = State
    Assert ($s.State -eq 0 -and $s.Phase -eq 0 -and $s.Session -eq 0) 'after the long break the round is Ready again'

    # --- Reset ---
    Cmd $CMDP.Toggle; EndPhase
    Cmd $CMDP.Reset
    $s = State
    Assert ($s.State -eq 0 -and $s.Phase -eq 0 -and $s.Session -eq 0 -and $s.Left -eq 1500) 'Reset goes back to focus 1 of 4, Ready'
    Assert ((Pomo 3) -eq 125) 'focus minutes already done stay counted'

    # --- durations and sounds from settings.ini ---
    Wait-Quiet
    Set-Content -LiteralPath $ini -Value '[Pomodoro]', 'Focus=50', 'ShortBreak=10', 'LongBreak=30', '[Sounds]', 'Reminders=0' -Encoding ASCII
    Assert (Wait-Until { (State).Left -eq 3000 } 3000) "Focus=50 makes the session 50:00 ($((State).Left) s)"
    Cmd $CMDP.Toggle
    $chimes = Pomo 2
    EndPhase
    Assert ((State).Left -le 600 -and (State).Left -ge 590) 'ShortBreak=10'
    Assert ((Get-ToastText) -eq 'Focus finished | Take a 10 min break') "pop-up: $(Get-ToastText)"
    Assert ((Pomo 2) -eq $chimes) 'Reminders=0: no chime'
    Cmd $CMDP.Reset
    Set-Content -LiteralPath $ini -Value '[Pomodoro]', 'Focus=0' -Encoding ASCII
    Assert (Wait-Until { (Get-ToastText) -like '*Focus: use 1-120*' } 3000) "an out-of-range length is reported ($(Get-ToastText))"

    # --- budget: Ready costs nothing; a running session only has one timer while closed ---
    Remove-Item $ini
    Cmd $CMDP.Toggle
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 7000 | Out-Null
    Start-Sleep -Seconds 2
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 30) "idle CPU ~0 while a session runs with the notch closed ($([int]$cpu) ms over 5 s)"
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'pomodoro'
