# Calendar: the To-do page's calendar button, Month and Week views, and events
# from an iCal link. A small web server on 127.0.0.1 serves a test calendar made around today:
# a timed event, an all-day event, a weekly event with a skipped date (EXDATE), a daily event with
# one repeat moved (RECURRENCE-ID), a UTC event, "last Friday of the month", a cancelled event and a
# folded, escaped title. Checks what Perch finds on each day, the cached copy, a page that isn't a
# calendar, an address that can't be reached, Paste calendar link (the clipboard is put back) and
# Remove. Clicks are sent to the notch's own window while the pointer is over it (put back after).
# Uses its own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Calendar =='
if (-not ('CalProbe' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class CalProbe {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
}
$work = Join-Path $env:TEMP "perch-calendar-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work

function Cal([int]$wp = 0, [int]$lp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 29, [IntPtr]$wp, [IntPtr]$lp) }
function Status { $v = Cal; [pscustomobject]@{ Status = $v -band 15; Events = $v -shr 4 } }
function View { $v = Cal 2; [pscustomobject]@{ Open = ($v -band 1) -ne 0; Week = ($v -band 2) -ne 0; Sel = ($v -shr 4) - 1000 } }
function Hot-Client([int]$hot) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 20, [IntPtr]$hot, [IntPtr]::Zero) }
function Tap([int]$hot) { [CalProbe]::PostMessage((Get-NotchWindow), 0x202, [IntPtr]0, [IntPtr](Hot-Client $hot)) | Out-Null; Start-Sleep -Milliseconds 150 }
$H = @{ Cal = 44; Prev = 53; Next = 54; Title = 55; Day0 = 230 }

# ---- the test calendar, built around today ----
$d0 = (Get-Date).Date
function D([int]$off) { $d0.AddDays($off).ToString('yyyyMMdd') }
$fri = @() # "last Friday of the month" from 60 days ago to 60 days ahead
for ($k = -3; $k -le 3; $k++) {
    $m1 = (Get-Date -Year $d0.Year -Month $d0.Month -Day 1).Date.AddMonths($k + 1)
    $last = $m1.AddDays(-1); while ($last.DayOfWeek -ne 'Friday') { $last = $last.AddDays(-1) }
    $off = ($last - $d0).Days
    if ($off -ge -60 -and $off -le 60) { $fri += $off }
}
$fri = @($fri | Sort-Object)
$utc = $d0.AddDays(2).AddHours(12).ToUniversalTime() # 12:00 local on day +2, written in UTC
$ics = @"
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//Perch test//EN
BEGIN:VEVENT
UID:standup
DTSTART:$(D 0)T100000
DTEND:$(D 0)T110000
SUMMARY:Standup
END:VEVENT
BEGIN:VEVENT
UID:holiday
DTSTART;VALUE=DATE:$(D 1)
DTEND;VALUE=DATE:$(D 2)
SUMMARY:Holiday
END:VEVENT
BEGIN:VEVENT
UID:weekly
DTSTART;TZID=Asia/Kolkata:$(D -14)T090000
DTEND;TZID=Asia/Kolkata:$(D -14)T093000
RRULE:FREQ=WEEKLY;COUNT=5
EXDATE;TZID=Asia/Kolkata:$(D 7)T090000
SUMMARY:Weekly review
BEGIN:VALARM
ACTION:DISPLAY
DESCRIPTION:not an event
TRIGGER:-PT10M
END:VALARM
END:VEVENT
BEGIN:VEVENT
UID:gym
DTSTART:$(D -1)T180000
DURATION:PT1H
RRULE:FREQ=DAILY;COUNT=4
SUMMARY:Gym
END:VEVENT
BEGIN:VEVENT
UID:gym
RECURRENCE-ID:$(D 1)T180000
DTSTART:$(D 3)T180000
DTEND:$(D 3)T190000
SUMMARY:Gym (moved)
END:VEVENT
BEGIN:VEVENT
UID:utc
DTSTART:$($utc.ToString('yyyyMMddTHHmmss'))Z
DTEND:$($utc.AddHours(1).ToString('yyyyMMddTHHmmss'))Z
SUMMARY:Call
END:VEVENT
BEGIN:VEVENT
UID:lastfri
DTSTART:$(D $fri[0])T160000
DTEND:$(D $fri[0])T170000
RRULE:FREQ=MONTHLY;BYDAY=-1FR;UNTIL=$(D 60)T235959Z
SUMMARY:Month end
END:VEVENT
BEGIN:VEVENT
UID:cancelled
DTSTART:$(D 0)T150000
DTEND:$(D 0)T160000
STATUS:CANCELLED
SUMMARY:Cancelled
END:VEVENT
BEGIN:VEVENT
UID:lunch
DTSTART:$(D 4)T130000
DTEND:$(D 4)T140000
SUMMARY:Lunch\, with the team and a title long enough to be folded onto a seco
 nd line
END:VEVENT
END:VCALENDAR
"@ -replace "`r?`n", "`r`n"

$expect = @{}
function Add([int]$off) { $expect[$off] = 1 + [int]$expect[$off] }
Add 0; Add 1; foreach ($o in -14, -7, 0, 14) { Add $o }; foreach ($o in -1, 0, 2, 3) { Add $o }; Add 2; Add 4
foreach ($o in $fri) { Add $o }
# tasks: one daily (every day from today), one for today
Set-Content -LiteralPath (Join-Path $work 'tasks.txt') -Value "0`t1`t0`t-1`t-1`t0`tWater plants", "0`t0`t0`t600`t630`t0`tCall mum" -Encoding UTF8
for ($o = 0; $o -le 30; $o++) { $expect[$o] = [int]$expect[$o] + 1 }; $expect[0] += 1

# ---- a web server on 127.0.0.1 ----
$sync = [hashtable]::Synchronized(@{ Ics = $ics; Hits = 0 })
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$server = [powershell]::Create()
$server.AddScript({
    param($l, $sync)
    while ($true) {
        try { $c = $l.AcceptTcpClient() } catch { break }
        try {
            $s = $c.GetStream(); $r = New-Object IO.StreamReader($s)
            $line = $r.ReadLine(); while (($h = $r.ReadLine()) -ne $null -and $h -ne '') {}
            $path = ($line -split ' ')[1]; $sync.Hits++
            if ($path -eq '/cal.ics') { $body = $sync.Ics; $code = '200 OK' }
            elseif ($path -eq '/page') { $body = '<html><body>Not a calendar</body></html>'; $code = '200 OK' }
            else { $body = 'missing'; $code = '404 Not Found' }
            $b = [Text.Encoding]::UTF8.GetBytes($body)
            $hd = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $code`r`nContent-Type: text/calendar`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n")
            $s.Write($hd, 0, $hd.Length); $s.Write($b, 0, $b.Length); $s.Flush()
        } catch {} finally { $c.Close() }
    }
}).AddArgument($listener).AddArgument($sync) | Out-Null
$serverRun = $server.BeginInvoke()
$url = "http://127.0.0.1:$port/cal.ics"

$cursor = Save-Cursor
$clip = $null; try { $clip = Get-Clipboard -Raw -ErrorAction Stop } catch {}
& {
try {
    Set-Content -LiteralPath $ini -Value '[Calendar]', "Link=$url" -Encoding ASCII
    Start-Notch

    # --- download and what's on each day ---
    Assert (Wait-Until { (Status).Status -eq 2 } 8000) "connected to the test calendar (status $((Status).Status))"
    Assert ((Status).Events -gt 0) "events read: $((Status).Events)"
    $bad = @()
    for ($o = -20; $o -le 30; $o++) { $got = Cal 1 $o; if ($got -ne [int]$expect[$o]) { $bad += "day $o got $got want $([int]$expect[$o])" } }
    Assert ($bad.Count -eq 0) "items on each day from -20 to +30: weekly with a skipped date, moved repeat, UTC, last Friday, all-day, cancelled, daily task ($($bad -join '; '))"
    Assert (Test-Path (Join-Path $work 'calendar.ics')) 'the download is kept for next time (calendar.ics)'

    # --- the calendar view ---
    [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]3, [IntPtr]::Zero) | Out-Null # To-do page
    if (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) {
        Start-Sleep -Milliseconds 300
        Tap $H.Cal
        $v = View
        Assert ($v.Open -and $v.Sel -eq 0 -and -not $v.Week) "calendar button opens the calendar on today, Month view (open $($v.Open), day $($v.Sel))"
        Save-TopShot 'calendar_month' | Out-Null
        $month = Cal 4
        Tap ($H.Day0 + 10); $a = (View).Sel
        Tap ($H.Day0 + 11); $b = (View).Sel
        Assert ($b -eq $a + 1) "tapping a day selects it ($a then $b)"
        Tap $H.Next
        $m2 = Cal 4
        Assert ((($m2 % 100) -eq (($month % 100) % 12) + 1)) "next month ($month -> $m2)"
        Tap $H.Prev; Tap $H.Prev
        Assert ((Cal 4) -ne $month -and (Cal 4) -ne $m2) "previous month ($(Cal 4))"
        Tap $H.Title
        Assert ((View).Week) 'tapping the month name switches to Week'
        Save-TopShot 'calendar_week' | Out-Null
        Tap $H.Title
        Assert (-not (View).Week) '...and back to Month'
        Tap $H.Cal
        Assert (-not (View).Open) 'the calendar button goes back to the tasks'
    } else { Write-Host '  SKIP view: the notch did not open under the pointer' -ForegroundColor Yellow }
    Restore-Cursor $cursor
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null

    # --- a page that isn't a calendar; an address that can't be reached ---
    Set-Content -LiteralPath $ini -Value '[Calendar]', "Link=http://127.0.0.1:$port/page" -Encoding ASCII
    Assert (Wait-Until { (Status).Status -eq 4 } 8000) "a web page that isn't a calendar is refused (status $((Status).Status))"
    Assert ((Status).Events -eq 0 -and (Cal 1 0) -eq 2) 'no events from it (only the tasks)'
    Set-Content -LiteralPath $ini -Value '[Calendar]', 'Link=http://127.0.0.1:1/cal.ics' -Encoding ASCII
    Assert (Wait-Until { (Status).Status -eq 3 } 15000) "an address that can't be reached (status $((Status).Status))"

    # --- Paste calendar link (right-click menu) ---
    Set-Clipboard -Value "  $url  "
    [NotchTest]::Command((Get-NotchWindow), 112)
    Assert (Wait-Until { (Get-ToastText) -like 'Calendar connected*' } 8000) "Paste calendar link: $(Get-ToastText)"
    Assert ((Get-Content $ini) -contains "Link=$url") 'the pasted link is saved to settings.ini'
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Set-Clipboard -Value 'not a link'
    [NotchTest]::Command((Get-NotchWindow), 112)
    Assert (Wait-Until { (Get-ToastText) -like 'No link on the clipboard*' } 3000) "text that isn't a link is refused: $(Get-ToastText)"
    Assert ((Get-Content $ini) -contains "Link=$url") '...and the saved link stays'

    # --- Remove calendar link ---
    [NotchTest]::Command((Get-NotchWindow), 114)
    Assert (Wait-Until { (Status).Status -eq 0 } 3000) 'Remove calendar link'
    Assert (-not (Test-Path (Join-Path $work 'calendar.ics'))) '...deletes the kept copy'

    # --- budget ---
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 7000 | Out-Null
    Start-Sleep -Seconds 2
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 30) "idle CPU ~0 ($([int]$cpu) ms over 5 s)"
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    if ($null -ne $clip) { Set-Clipboard -Value $clip } else { Set-Clipboard -Value $null -ErrorAction SilentlyContinue }
    $listener.Stop()
    $server.Dispose()
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'calendar'
