# To-do page : Add Task with the inline editor (real click, typing), bad times refused,
# ticking a task done, Edit mode (reorder, delete), tasks.txt, "Task Time!" (Later / Start) and
# "Time's Up!" (+10 min / Done) reminders with snooze, reminders waiting their turn, RemindStart=0,
# one-off tasks done on an earlier day dropped, the Tasks ring. Reminders are checked "as if" at a
# given minute with a test message instead of waiting. Moves the pointer over the notch (puts it back)
# and types only when the notch has the keyboard. Uses its own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== To-do =='
if (-not ('TodoProbe' -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Runtime.InteropServices;
public static class TodoProbe {
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(System.Drawing.Point p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
}
$work = Join-Path $env:TEMP "perch-todo-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$tasks = Join-Path $work 'tasks.txt'
$env:PERCH_DATA_DIR = $work
$Ctl = @{ Add = 42; Edit = 43; Title = 45; Time = 46; Daily = 47; Cancel = 48; Save = 49; A = 51; B = 52; Row0 = 60; Up0 = 100; Down0 = 140; Del0 = 180 }
function Todo([int]$wp = 0, [int]$lp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 27, [IntPtr]$wp, [IntPtr]$lp) }
function St { $v = Todo; [pscustomobject]@{ Tasks = $v -band 255; Done = ($v -shr 8) -band 255; Editor = ($v -shr 16) -band 15; Reminder = ($v -shr 20) -band 15; EditMode = ($v -shr 24) -band 1 } }
function Hot-Client([int]$hot) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 20, [IntPtr]$hot, [IntPtr]::Zero) }
function Tap([int]$hot) { [TodoProbe]::PostMessage((Get-NotchWindow), 0x202, [IntPtr]0, [IntPtr](Hot-Client $hot)) | Out-Null; Start-Sleep -Milliseconds 150 }
function Type-Text([string]$t) { foreach ($c in $t.ToCharArray()) { [TodoProbe]::PostMessage((Get-NotchWindow), 0x102, [IntPtr][int]$c, [IntPtr]0) | Out-Null }; Start-Sleep -Milliseconds 150 }
function Key([int]$vk) { [TodoProbe]::PostMessage((Get-NotchWindow), 0x100, [IntPtr]$vk, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 150 }
function Open-Todo { [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]3, [IntPtr]::Zero) | Out-Null; $ok = Wait-Until { Hover-Notch; Test-Flag Expanded } 3000; [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), 110) | Out-Null; Start-Sleep -Milliseconds 400; $ok }
function Lines { if (Test-Path $tasks) { return ,@(Get-Content $tasks -Encoding UTF8 | Where-Object { $_ }) } else { return ,@() } }
function Answer([int]$hot) { # a reminder's buttons only respond once it has opened
    Wait-Until { Test-Flag Expanded } 2000 | Out-Null; Start-Sleep -Milliseconds 250; Tap $hot
}
function Wait-Quiet { Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null }
$now = (Get-Date).Hour * 60 + (Get-Date).Minute
$s1 = [math]::Min($now + 30, 1400); $e1 = $s1 + 15
function HHMM([int]$m) { '{0:D2}{1:D2}' -f [int][math]::Floor($m / 60), [int]($m % 60) }
$today = (Get-Date).ToString('yyyyMMdd')

$cursor = Save-Cursor
& {
try {
    Start-Notch
    $s = St
    Assert ($s.Tasks -eq 0 -and $s.Done -eq 0) 'no tasks to start with (0/0)'
    if (-not (Open-Todo)) { Write-Host '  SKIP  the notch did not open (covered?)' -ForegroundColor Yellow; return }
    Write-Host "  screenshot: $(Save-TopShot 'todo-empty')"

    # --- Add Task: a real click, then typing ---
    $h = Get-NotchWindow; $r = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect($h, [ref]$r) | Out-Null
    $v = Hot-Client $Ctl.Add; $x = $r.Left + [int]($v -band 0xFFFF); $y = $r.Top + [int](($v -shr 16) -band 0xFFFF)
    [NotchTest]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 900 # let it settle
    if ([TodoProbe]::GetAncestor([TodoProbe]::WindowFromPoint((New-Object System.Drawing.Point $x, $y)), 2) -eq $h) {
        [TodoProbe]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [TodoProbe]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
        Assert (Wait-Until { (St).Editor -eq 1 } 1500) 'clicking Add Task opens the editor'
        Assert (Wait-Until { [TodoProbe]::GetForegroundWindow() -eq $h } 1500) '...and the notch takes the keyboard'
        if ([TodoProbe]::GetForegroundWindow() -eq $h) {
            [System.Windows.Forms.SendKeys]::SendWait('Buy milk'); Start-Sleep -Milliseconds 200
            [System.Windows.Forms.SendKeys]::SendWait('{TAB}'); [System.Windows.Forms.SendKeys]::SendWait("$(HHMM $s1)$(HHMM $e1)"); Start-Sleep -Milliseconds 200
            Write-Host "  screenshot: $(Save-TopShot 'todo-editor')"
            [System.Windows.Forms.SendKeys]::SendWait('{ENTER}'); Start-Sleep -Milliseconds 300
        } else { Write-Host '  SKIP  typing (the notch does not have the keyboard)' -ForegroundColor Yellow; Type-Text 'Buy milk'; Key 9; Type-Text "$(HHMM $s1)$(HHMM $e1)"; Key 13 }
    } else {
        Write-Host '  SKIP  something else is over Add Task; using posted clicks' -ForegroundColor Yellow
        Tap $Ctl.Add; Type-Text 'Buy milk'; Key 9; Type-Text "$(HHMM $s1)$(HHMM $e1)"; Key 13
    }
    [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), 110) | Out-Null
    $s = St
    Assert ($s.Editor -eq 0 -and $s.Tasks -eq 1) "Enter saves the task (tasks $($s.Tasks))"
    Assert ((Lines)[0] -eq "0`t0`t0`t$s1`t$e1`t0`tBuy milk") "tasks.txt: '$((Lines)[0])'"
    Write-Host "  screenshot: $(Save-TopShot 'todo-one')"

    # --- bad input is refused ---
    Tap $Ctl.Add; Type-Text 'Late'; Key 9; Type-Text '25001200'; Key 13
    Assert ((St).Editor -eq 1 -and (St).Tasks -eq 1) 'a time of 25:00 is refused and the editor stays open'
    Key 0x1B
    Assert ((St).Editor -eq 0 -and (St).Tasks -eq 1) 'Esc closes it without saving'
    Tap $Ctl.Add; Key 13
    Assert ((St).Editor -eq 1) 'an empty title is refused'
    Tap $Ctl.Cancel
    Assert ((St).Editor -eq 0) 'Cancel closes the editor'

    # --- a second task, no time, Daily ---
    Tap $Ctl.Add; Type-Text 'Stretch'; Tap $Ctl.Daily; Tap $Ctl.Save
    Assert ((St).Tasks -eq 2 -and (Lines)[1] -eq "0`t1`t0`t-1`t-1`t0`tStretch") "Save adds a Daily task with no time: '$((Lines)[1])'"

    # --- ticking ---
    Tap $Ctl.Row0
    Assert ((St).Done -eq 1 -and (Lines)[0].StartsWith("$today`t")) 'clicking a task ticks it (done today)'
    Write-Host "  screenshot: $(Save-TopShot 'todo-done')"
    Tap $Ctl.Row0
    Assert ((St).Done -eq 0) 'clicking again unticks it'

    # --- Edit mode: reorder, edit, delete ---
    Tap $Ctl.Edit
    Assert ((St).EditMode -eq 1) 'Edit Task shows the row buttons'
    Write-Host "  screenshot: $(Save-TopShot 'todo-editmode')"
    Tap ($Ctl.Down0 + 0)
    Assert ((Lines)[0] -like '*Stretch' -and (Lines)[1] -like '*Buy milk') 'the down button moves a task down'
    Tap ($Ctl.Up0 + 1)
    Assert ((Lines)[0] -like '*Buy milk') 'the up button moves it back'
    Tap ($Ctl.Row0 + 1)
    Assert ((St).Editor -eq 2) 'clicking a task in Edit mode opens it in the editor'
    Type-Text ' daily'; Key 13
    Assert ((Lines)[1] -like '*Stretch daily') "...and Save changes it ('$((Lines)[1])')"
    Tap ($Ctl.Del0 + 1) # still in Edit mode
    Assert ((St).Tasks -eq 1 -and (Lines).Count -eq 1) 'the red button deletes a task'
    Tap $Ctl.Edit
    Assert ((St).EditMode -eq 0) 'Edit Task again leaves Edit mode'

    # --- reminders ---
    [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), 700) | Out-Null
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null
    $chimes = [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 26, [IntPtr]2, [IntPtr]::Zero)
    Todo 1 ($s1 - 1) | Out-Null
    Assert ((St).Reminder -eq 0) 'nothing before the start time'
    Todo 1 $s1 | Out-Null
    Assert ((St).Reminder -eq 1 -and (Get-ToastText) -eq 'Task Time! | Buy milk') "at the start time: $(Get-ToastText)"
    Assert ([int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 26, [IntPtr]2, [IntPtr]::Zero) -eq $chimes + 1) '...with a chime'
    Assert (Wait-Until { Test-Flag Expanded } 2000) '...and it opens on its own'
    Start-Sleep -Milliseconds 500
    Write-Host "  screenshot: $(Save-TopShot 'todo-tasktime')"
    Start-Sleep -Milliseconds 1500
    Assert ((St).Reminder -eq 1 -and (Test-Flag Expanded)) 'it stays until answered'
    Answer $Ctl.A # Later
    Assert ((St).Reminder -eq 0) 'Later closes it'
    Todo 1 ($s1 + 9) | Out-Null
    Assert ((St).Reminder -eq 0) '...not again before the snooze (10 min) is over'
    Todo 1 ($s1 + 10) | Out-Null
    Assert ((St).Reminder -eq 1) '...and again after 10 min'
    Answer $Ctl.B # Start
    Assert ((St).Reminder -eq 0) 'Start closes it'
    Todo 1 $e1 | Out-Null
    Assert ((St).Reminder -eq 2 -and (Get-ToastText) -eq "Time's Up! | Buy milk") "at the end time: $(Get-ToastText)"
    Start-Sleep -Milliseconds 600
    Write-Host "  screenshot: $(Save-TopShot 'todo-timesup')"
    Answer $Ctl.A # +10 min
    Assert ((Lines)[0] -match "`t$($e1 + 10)`t") "+10 min moves the end 10 minutes later ('$((Lines)[0])')"
    Todo 1 ($e1 + 10) | Out-Null
    Assert ((St).Reminder -eq 2) "Time's Up! again at the new end"
    Answer $Ctl.B # Done
    Assert ((St).Reminder -eq 0 -and (St).Done -eq 1) 'Done ticks the task'

    # --- two at once wait their turn ---
    Wait-Quiet
    Set-Content -LiteralPath $tasks -Value "0`t0`t0`t$s1`t-1`t0`tOne", "0`t0`t0`t$s1`t-1`t0`tTwo" -Encoding UTF8
    Stop-Notch; Start-Notch
    Todo 1 $s1 | Out-Null
    Assert ((Get-ToastText) -eq 'Task Time! | One' -and (Todo 2) -eq 1) 'two reminders at once: the first shows, the second waits'
    Answer $Ctl.B
    Assert ((Get-ToastText) -eq 'Task Time! | Two') '...then the second'
    Answer $Ctl.B

    # --- RemindStart=0, old one-off tasks, Tasks ring ---
    Wait-Quiet
    Set-Content -LiteralPath $tasks -Value "0`t0`t0`t$s1`t-1`t0`tQuiet", "20200101`t0`t0`t-1`t-1`t0`tOld done", "20200101`t1`t0`t-1`t-1`t0`tDaily old" -Encoding UTF8
    Set-Content -LiteralPath $ini -Value '[ToDo]', 'RemindStart=0', '[Notch]', 'Ring=Tasks' -Encoding ASCII
    Stop-Notch; Start-Notch
    $s = St
    Assert ($s.Tasks -eq 2 -and -not ((Lines) -like '*Old done')) 'a one-off task done on an earlier day is dropped; a daily one stays, unticked'
    Todo 1 $s1 | Out-Null
    Assert ((St).Reminder -eq 0) 'RemindStart=0: no Task Time! reminder'
    Assert ([int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]12, [IntPtr]::Zero) -eq 2) 'Ring=Tasks is read'
    Set-Content -LiteralPath $ini -Value '[ToDo]', 'Snooze=7' -Encoding ASCII
    Assert (Wait-Until { (Get-ToastText) -like '*Snooze: use 5, 10, 15*' } 3000) "a bad snooze value is reported ($(Get-ToastText))"

    # --- budget ---
    Remove-Item $ini
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 7000 | Out-Null
    Start-Sleep -Seconds 2
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 30) "idle CPU ~0 with tasks waiting ($([int]$cpu) ms over 5 s)"
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'todo'
