# Water page: today's water (+ a cup, take one back) with a real click, water.txt kept across a restart,
# reminders spread over the active hours ("as if" at a given minute, by a test message), Later / Drank
# on the pop-up, no reminders once off, the Water ring, and the idle cost. Uses its own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Water =='
if (-not ('WaterProbe' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class WaterProbe {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
}
$work = Join-Path $env:TEMP "perch-water-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
Set-Content -LiteralPath $ini -Value '[Notch]', 'Ring=Water', '[Water]', 'Reminders=1', 'Goal=2000', 'Cup=250', 'From=08:00', 'Until=17:00' -Encoding ASCII
$Hot = @{ Add = 56; Undo = 57; A = 51; B = 52 }
function Water([int]$wp = 0, [int]$lp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 33, [IntPtr]$wp, [IntPtr]$lp) }
function Hot-Client([int]$hot) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 20, [IntPtr]$hot, [IntPtr]::Zero) }
function Tap([int]$hot) { [WaterProbe]::PostMessage((Get-NotchWindow), 0x202, [IntPtr]0, [IntPtr](Hot-Client $hot)) | Out-Null; Start-Sleep -Milliseconds 200 }
function Open-Water { [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]4, [IntPtr]::Zero) | Out-Null; $ok = Wait-Until { Hover-Notch; Test-Flag Expanded } 3000; [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), 110) | Out-Null; Start-Sleep -Milliseconds 500; $ok }
function Away { [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), [int]((Get-Screen).Height / 2)) | Out-Null }
$today = (Get-Date).ToString('yyyyMMdd')
function TodayLine { if (Test-Path (Join-Path $work 'water.txt')) { (Get-Content (Join-Path $work 'water.txt') | Where-Object { $_ -like "$today *" }) } }

$cursor = Save-Cursor
& {
try {
    Start-Notch
    Assert ((Water) -eq 0) 'no water yet today'
    Assert ((Water 1 250) -eq 250 -and (TodayLine) -eq "$today 250") '+250 ml is kept in water.txt'

    # the page: + and - are real buttons
    if (Open-Water) {
        Write-Host "  screenshot: $(Save-TopShot 'water-page')"
        Tap $Hot.Add
        Assert ((Water) -eq 500) "the + button adds a cup ($(Water) ml)"
        Tap $Hot.Undo
        Assert ((Water) -eq 250) 'the - button takes one back'
        Tap $Hot.Undo; Tap $Hot.Undo
        Assert ((Water) -eq 0) '...never below 0'
        Water 1 250 | Out-Null
    } else { Write-Host '  SKIP  the notch did not open (covered?)' -ForegroundColor Yellow }
    Away
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null

    # reminders: 8 cups of 250 ml over 08:00-17:00, one every 67.5 min (09:07, 10:15, 11:22, ...)
    Assert ((Water 2 600) -eq 0) 'at 10:00 with 250 ml: on track, no reminder'
    Assert ((Water 3) -eq 615) "...the next one would be at 10:15 ($(Water 3))"
    Assert ((Water 2 700) -eq 1) 'at 11:40 with 250 ml (behind): "Time for water"'
    Assert ((Get-ToastText) -eq 'Time for water | 250 ml of 2 L so far') "pop-up: $(Get-ToastText)"
    Wait-Until { Test-Flag Expanded } 2000 | Out-Null; Start-Sleep -Milliseconds 300
    Tap $Hot.B
    Assert ((Water) -eq 500 -and (Wait-Until { -not (Test-Flag Toast) } 2000)) 'Drank adds a cup and closes it'
    Assert ((Water 3) -eq 750) "the next one is at 12:30 ($(Water 3))"
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null
    Assert ((Water 2 760) -eq 1) 'at 12:40, behind again: another reminder'
    Wait-Until { Test-Flag Expanded } 2000 | Out-Null; Start-Sleep -Milliseconds 300
    Tap $Hot.A
    Assert ((Wait-Until { -not (Test-Flag Toast) } 2000) -and (Water) -eq 500) 'Later closes it without adding'
    Assert ((Water 3) -eq 770) "...and asks again 10 min later ($(Water 3))"
    Assert ((Water 2 1100) -eq 0) 'not after the active hours (18:20)'
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null

    # Reminders=0
    Set-Content -LiteralPath $ini -Value '[Notch]', 'Ring=Water', '[Water]', 'Reminders=0' -Encoding ASCII
    Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge 2 } 3000 | Out-Null
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Assert ((Water 2 700) -eq 0 -and (Water 3) -eq -1) 'Reminders=0: no reminders at all'

    # kept across a restart
    Stop-Notch; Start-Sleep -Milliseconds 300; Start-Notch
    Assert ((Water) -eq 500) "after a restart today's water is still there ($(Water) ml)"
    Write-Host "  screenshot: $(Save-TopShot 'water-ring')"

    Start-Sleep -Seconds 1
    $p = Get-Process Perch
    $c0 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $p.Refresh()
    $cpu = $p.TotalProcessorTime.TotalMilliseconds - $c0
    Assert ($cpu -lt 30) "idle CPU with the Water ring: $([int]$cpu) ms over 5 s"
    Assert ($p.WorkingSet64 / 1MB -lt 13) ("working set {0:N1} MB < 13 MB" -f ($p.WorkingSet64 / 1MB))
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'water'
