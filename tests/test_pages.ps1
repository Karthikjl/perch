# Pages (the open notch): header, Music, Pomodoro and Shelf pages, mouse-wheel paging,
# the < > arrows, the page icons that replace the dots on hover, and the mute button.
# Uses its own temporary data folder. Moves the mouse over the notch and clicks it (only after
# checking the notch is what's under the pointer), then puts the pointer back. Mutes and unmutes
# the speakers once.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Pages =='
if (-not ('PageProbe' -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Runtime.InteropServices;
public static class PageProbe {
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(System.Drawing.Point p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, UIntPtr e);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
}
$work = Join-Path $env:TEMP "notch-pages-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$env:PERCH_DATA_DIR = $work
$HOT = @{ ArrowL = 0; ArrowR = 1; Play = 5; Volume = 7; Settings = 10; Icon0 = 11 }
function Page([int]$set = -1) { [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]$set, [IntPtr]::Zero) }
function Get-Pill {
    $h = Get-NotchWindow
    $a = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 17, [IntPtr]0, [IntPtr]::Zero)
    $b = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 17, [IntPtr]1, [IntPtr]::Zero)
    [pscustomobject]@{ Left = [int]($a -band 0xFFFF); Top = [int](($a -shr 16) -band 0xFFFF); Right = [int]($b -band 0xFFFF); Bottom = [int](($b -shr 16) -band 0xFFFF) }
}
function Hot-Point([int]$hot) { # screen px of a control
    $h = Get-NotchWindow; $r = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect($h, [ref]$r) | Out-Null
    $v = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 20, [IntPtr]$hot, [IntPtr]::Zero)
    [pscustomobject]@{ X = $r.Left + [int]($v -band 0xFFFF); Y = $r.Top + [int](($v -shr 16) -band 0xFFFF) }
}
function Is-NotchAt([int]$x, [int]$y) { [PageProbe]::GetAncestor([PageProbe]::WindowFromPoint((New-Object System.Drawing.Point $x, $y)), 2) -eq (Get-NotchWindow) }
function Click-Hot([int]$hot) { # real click, but only on the notch itself
    $p = Hot-Point $hot
    [NotchTest]::SetCursorPos($p.X, $p.Y) | Out-Null; Start-Sleep -Milliseconds 350
    if (-not (Is-NotchAt $p.X $p.Y)) { return $false }
    [PageProbe]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [PageProbe]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500; return $true
}
function Wheel([int]$delta) { [PageProbe]::mouse_event(0x800, 0, 0, $delta, [UIntPtr]::Zero); Start-Sleep -Milliseconds 500 }

$cursor = Save-Cursor
& {
try {
    Start-Notch
    $scale = [math]::Round(((Get-Pill).Right - (Get-Pill).Left) / 150.0, 2) # closed pill is 150 DIPs wide
    $scr = Get-Screen
    Assert ((Page) -eq 0) 'the notch starts on the Music page'

    # --- open: its size ---
    Assert (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) 'hovering opens the notch'
    Start-Sleep -Milliseconds 300
    $p = Get-Pill
    $w = [math]::Round(($p.Right - $p.Left) / $scale); $hgt = [math]::Round(($p.Bottom - $p.Top) / $scale)
    Assert ([math]::Abs($w - 395.6) -le 2 -and [math]::Abs($hgt - 172) -le 2) "open notch is 395.6 x 172 DIPs ($w x $hgt)"
    Write-Host "  screenshot: $(Save-TopShot 'pages-music')"

    # --- wheel: down = next page, up = previous, and it wraps ---
    $mid = Hot-Point $HOT.Play
    [NotchTest]::SetCursorPos($mid.X, $mid.Y - 40) | Out-Null; Start-Sleep -Milliseconds 200
    if (Is-NotchAt $mid.X ($mid.Y - 40)) {
        Wheel -120; Assert ((Page) -eq 1) 'wheel down goes to the next page (Pomodoro)'
        Write-Host "  screenshot: $(Save-TopShot 'pages-pomodoro')"
        Wheel 120; Assert ((Page) -eq 0) 'wheel up goes back (Music)'
        Wheel 120; Assert ((Page) -eq 3) 'and wraps around from the first page to the last (To-do)'
    } else { Write-Host '  SKIP  something else is over the notch: wheel not tried' }

    # --- arrows ---
    Page 0 | Out-Null
    if (Click-Hot $HOT.ArrowR) { Assert (Wait-Until { (Page) -eq 1 } 1500) 'the > arrow shows the next page' } else { Write-Host '  SKIP  > arrow covered' }
    if (Click-Hot $HOT.ArrowL) { Assert (Wait-Until { (Page) -eq 0 } 1500) 'the < arrow shows the previous page' } else { Write-Host '  SKIP  < arrow covered' }

    # --- dots turn into page icons on hover ---
    $before = Get-Pill
    $dots = Hot-Point $HOT.Icon0
    [NotchTest]::SetCursorPos([int](($before.Left + $before.Right) / 2), $before.Bottom - [int](6 * $scale)) | Out-Null
    Start-Sleep -Milliseconds 700
    $after = Get-Pill
    $grow = [math]::Round((($after.Bottom - $after.Top) - ($before.Bottom - $before.Top)) / $scale)
    Assert ($grow -ge 26) "hovering the dots grows the notch to show the page icons (+$grow DIPs)"
    if (Click-Hot ($HOT.Icon0 + 3)) { Assert (Wait-Until { (Page) -eq 2 } 1500) 'clicking the Shelf icon (4th, after Water) opens the Shelf page' } else { Write-Host '  SKIP  shelf icon covered' }
    if (Click-Hot ($HOT.Icon0 + 1)) { Assert (Wait-Until { (Page) -eq 1 } 1500) 'clicking the stopwatch icon opens the Pomodoro page' } else { Write-Host '  SKIP  Pomodoro icon covered' }
    Write-Host "  screenshot: $(Save-TopShot 'pages-icons')"
    [NotchTest]::SetCursorPos([int](($before.Left + $before.Right) / 2), $before.Bottom - [int](6 * $scale)) | Out-Null; Start-Sleep -Milliseconds 600
    if (Click-Hot $HOT.Settings) {
        Assert (Wait-Until { [NotchTest]::FindByClass('PerchSettings') -ne [IntPtr]::Zero } 3000) 'the gear icon opens Settings'
        [PageProbe]::PostMessage([NotchTest]::FindByClass('PerchSettings'), 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null # WM_CLOSE
    } else { Write-Host '  SKIP  gear icon covered' }

    # --- Music page: the speaker button mutes and unmutes ---
    Page 0 | Out-Null
    Assert (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) 'open again on Music'
    $muted = [NotchAudio]::GetMute()
    try {
        if (Click-Hot $HOT.Volume) {
            Assert (Wait-Until { [NotchAudio]::GetMute() -ne $muted } 1500) 'the speaker button toggles mute'
            Click-Hot $HOT.Volume | Out-Null
            Assert (Wait-Until { [NotchAudio]::GetMute() -eq $muted } 1500) '...and back'
        } else { Write-Host '  SKIP  speaker button covered' }
    } finally { if ([NotchAudio]::GetMute() -ne $muted) { [NotchAudio]::SetMute($muted) } }

    # --- page kept between opens ---
    Page 2 | Out-Null
    [NotchTest]::SetCursorPos([int]($scr.Width / 2), 700) | Out-Null
    Assert (Wait-Until { -not (Test-Flag Expanded) } 3000) 'moving away closes it'
    Assert (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) '...and it opens again'
    Assert ((Page) -eq 2) 'on the page it was showing (Shelf)'

    # --- budget ---
    [NotchTest]::SetCursorPos([int]($scr.Width / 2), 700) | Out-Null
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null; Start-Sleep -Milliseconds 1500
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 3; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 50) "idle CPU ~0 ($([int]$cpu) ms over 3 s)"
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'test_pages'
