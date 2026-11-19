# 1.1-1.3 Core notch: window appears, hover expands, collapses, idle cost.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Core notch =='
Start-Notch
$h = Get-NotchWindow
Assert ($h -ne [IntPtr]::Zero) 'notch window exists'
Assert ([NotchTest]::IsWindowVisible($h)) 'notch window is visible'
$r = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect($h, [ref]$r) | Out-Null
$b = Get-Screen
Assert ($r.Top -eq 0) 'window is at the top edge'
Assert ([math]::Abs((($r.Left + $r.Right) / 2) - $b.Width / 2) -le 1) 'window is horizontally centred'

# Pixels of Perch's window that aren't clear outside the closed notch: leftovers of the open notch, however faint.
function Get-Stray { [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 32, [IntPtr]::Zero, [IntPtr]::Zero) }

$cur = Save-Cursor
try {
    # Keeps putting the pointer on the notch while waiting, so a bump of the real mouse can't spoil the check.
    Assert (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) 'hover expands the notch'
    Save-TopShot 'core_expanded' | Out-Null
    # Move to the middle of the screen (the user's own cursor may be parked over the notch).
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int]($b.Height / 2)) | Out-Null
    Assert (Wait-Until { -not (Test-Flag Expanded) } 2500) 'moving away collapses it'
    Start-Sleep -Seconds 1
    $stray = Get-Stray
    Assert ($stray -eq 0) "nothing of the open notch is left after it closes ($stray pixels)"
    # Again with the page icons showing (pointer on the dots), leaving straight down: the icon bar slides away as it closes.
    $scale = ($r.Right - $r.Left) / 440.0
    Assert (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) 'hover expands the notch again'
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int](164 * $scale)) | Out-Null; Start-Sleep -Milliseconds 500
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int]($b.Height / 2)) | Out-Null
    Assert (Wait-Until { -not (Test-Flag Expanded) } 2500) 'it closes from the page icons'
    Start-Sleep -Seconds 1
    $stray = Get-Stray
    Assert ($stray -eq 0) "nothing of the page icons is left after closing from them ($stray pixels)"
    Assert (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) 'hover expands the notch a third time'
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int](164 * $scale)) | Out-Null; Start-Sleep -Milliseconds 500
    [NotchTest]::SetCursorPos([int]($b.Width / 2) + [int](150 * $scale), [int](164 * $scale)) | Out-Null; Start-Sleep -Milliseconds 80
    [NotchTest]::SetCursorPos([int]($b.Width / 2) + [int](400 * $scale), [int](164 * $scale)) | Out-Null
    Assert (Wait-Until { -not (Test-Flag Expanded) } 2500) 'it closes when leaving the page icons sideways'
    Start-Sleep -Seconds 1
    $stray = Get-Stray
    Assert ($stray -eq 0) "nothing is left after leaving the page icons sideways ($stray pixels)"
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int]($b.Height / 2)) | Out-Null
    $p = Get-Process Perch
    $ws = $p.WorkingSet64 / 1MB
    Assert ($ws -lt 13) ("idle working set {0:N1} MB < 13 MB (private {1:N1} MB)" -f $ws, ($p.PrivateMemorySize64 / 1MB))
    $c1 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $p.Refresh()
    $cpu = $p.TotalProcessorTime.TotalMilliseconds - $c1
    Assert ($cpu -lt 20) ("idle CPU over 5 s: {0:N0} ms (~0%)" -f $cpu)
} finally { Restore-Cursor $cur }
Finish-Test 'core'
