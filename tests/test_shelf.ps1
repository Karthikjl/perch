# 5.1 Shelf: files go onto the notch (command line and real drag & drop), stay across
# restarts, and can be dragged out into another app. Uses its own temporary shelf
# (PERCH_DATA_DIR), so your real shelf is untouched. Moves the mouse and holds the
# left button for a few seconds.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 5.1 Shelf =='

$work = Join-Path $env:TEMP "notch-shelf-test-$PID"
$files = Join-Path $work 'files'; $data = Join-Path $work 'data'
New-Item -ItemType Directory -Force $files, $data | Out-Null
'alpha' | Set-Content "$files\a.txt"; 'beta' | Set-Content "$files\b.txt"; 'gamma' | Set-Content "$files\c.txt"
$bmp = New-Object System.Drawing.Bitmap 120, 80
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(40, 170, 255)); $g.Dispose()
$bmp.Save("$files\photo.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

$env:PERCH_DATA_DIR = $data
function Show-ShelfPage { [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]2, [IntPtr]::Zero) | Out-Null } # tiles live on the Shelf page (third)
function Get-ShelfCount { [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 11, [IntPtr]::Zero, [IntPtr]::Zero) }
function Get-ShelfFile { if (Test-Path "$data\shelf.txt") { Get-Content "$data\shelf.txt" -Encoding Unicode } }
$cur = Save-Cursor
$b = Get-Screen
& { # (a script block, so an early 'return' still reaches Finish-Test)
try {
    Start-Notch
    Assert ((Get-ShelfCount) -eq 0) 'shelf starts empty'

    # --- command line: Perch.exe --shelf ---
    & $Exe --shelf "$files\a.txt" "$files\b.txt"
    Assert (Wait-Until { (Get-ShelfCount) -eq 2 } 3000) "'Perch.exe --shelf a.txt b.txt' adds 2 files ($(Get-ShelfCount))"
    Assert ((Get-ShelfFile) -contains "$files\a.txt") 'shelf is saved to disk'
    & $Exe --shelf "$files\a.txt"; Start-Sleep -Milliseconds 400
    Assert ((Get-ShelfCount) -eq 2) 'adding the same file twice keeps one copy'

    # --- real drag & drop onto the notch ---
    # (DragFilesTo returns 'NotOnTop' and does nothing if its own window can't get on top.)
    # The drag starts on the test's own small window just below the notch and moves straight up.
    $effect = [NotchDrag]::DragFilesTo(@("$files\photo.png", "$files\c.txt"), [int]($b.Width / 2), 320, [int]($b.Width / 2), 12)
    $expected = 4
    if ($effect -eq 'NotOnTop') {
        # Safety: the test's own drag window is covered (e.g. by the app you're using), so it
        # won't press the mouse at all. Add the files the other way and carry on.
        Write-Host '  SKIP  drag & drop onto the notch: test window is covered by another window'
        & $Exe --shelf "$files\photo.png" "$files\c.txt"
    } else {
        Assert ($effect -eq 'Copy') "dropping files on the notch is accepted (effect: $effect)"
    }
    Assert (Wait-Until { (Get-ShelfCount) -eq $expected } 2000) "dropped files land on the shelf ($(Get-ShelfCount) items)"

    Hover-Notch; Show-ShelfPage
    Assert (Wait-Until { [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 12, [IntPtr]::Zero, [IntPtr]::Zero) -ne -1 } 3000) 'hovering shows the shelf tiles'
    Start-Sleep -Milliseconds 400
    Save-TopShot 'shelf_expanded' | Out-Null
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int]($b.Height / 2)) | Out-Null
    Wait-Until { -not (Test-Flag Expanded) } 2500 | Out-Null

    # --- survives a restart ---
    Start-Notch
    Assert ((Get-ShelfCount) -eq 4) "shelf is restored after a restart ($(Get-ShelfCount) items)"

    # --- the x on a tile removes it from the shelf (the file itself stays) ---
    'delta' | Set-Content "$files\d.txt"
    & $Exe --shelf "$files\d.txt" # a 5th tile, so there's one to remove
    Assert (Wait-Until { (Get-ShelfCount) -eq 5 } 2000) 'a fifth item for the remove test'
    Start-Sleep -Milliseconds 2200 # let the "added" flash finish first
    Hover-Notch; Show-ShelfPage
    $h = Get-NotchWindow
    Assert (Wait-Until { [NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 16, [IntPtr]4, [IntPtr]::Zero) -ne -1 } 3000) 'every tile has a remove button'
    Start-Sleep -Milliseconds 300
    Save-TopShot 'shelf_remove_buttons' | Out-Null
    $first = (Get-ShelfFile)[0]
    $cb = [int][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 16, [IntPtr]::Zero, [IntPtr]::Zero)
    $r = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect($h, [ref]$r) | Out-Null
    [NotchTest]::SetCursorPos($r.Left + ($cb -band 0xFFFF), $r.Top + (($cb -shr 16) -band 0xFFFF)) | Out-Null; Start-Sleep -Milliseconds 250
    [NotchDrag]::mouse_event([NotchDrag]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80
    [NotchDrag]::mouse_event([NotchDrag]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Assert (Wait-Until { (Get-ShelfCount) -eq 4 } 2000) "clicking a tile's x removes it ($(Get-ShelfCount) left)"
    Assert (-not ((Get-ShelfFile) -contains $first)) "...and it's gone from the saved list ($(Split-Path $first -Leaf))"
    Assert (Test-Path -LiteralPath $first) '...but the file itself is not deleted'
    [NotchTest]::SetCursorPos([int]($b.Width / 2), [int]($b.Height / 2)) | Out-Null
    Wait-Until { -not (Test-Flag Expanded) } 2500 | Out-Null

    # --- drag a tile out into another app ---
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'Notch drop test'; $form.TopMost = $true; $form.AllowDrop = $true
    $form.StartPosition = 'Manual'; $form.Bounds = New-Object System.Drawing.Rectangle ([int]($b.Width / 2 - 200)), ([int]($b.Height / 2 - 150)), 400, 300
    # .NET lists captured with GetNewClosure, so the event handlers fill the same objects we check.
    $received = New-Object System.Collections.ArrayList
    $events = New-Object System.Collections.ArrayList
    $form.Add_DragEnter({ param($s, $e) [void]$events.Add('enter'); $e.Effect = [System.Windows.Forms.DragDropEffects]::Copy }.GetNewClosure())
    $form.Add_DragDrop({ param($s, $e) [void]$events.Add('drop'); $f = $e.Data.GetData([System.Windows.Forms.DataFormats]::FileDrop); if ($f) { [void]$received.AddRange([string[]]$f) } }.GetNewClosure())
    $form.Add_FormClosing({ param($s, $e) [void]$events.Add("closing: $($e.CloseReason)") }.GetNewClosure())
    $form.Show(); [NotchDrag]::BringToFront($form)
    $dx = [int]($b.Width / 2); $dy = [int]($b.Height / 2)
    if (-not [NotchDrag]::IsTopAt($form, $dx + 3, $dy + 3)) {
        # Safety: if another window covers the drop point, don't drag anything onto it.
        Write-Host '  SKIP  drag out of the notch: test window is covered by another window'
        $form.Close(); $form.Dispose()
        [NotchTest]::Command((Get-NotchWindow), $CMD.ClearShelf)
        Assert ((Get-ShelfCount) -eq 0) 'Clear empties the shelf'
        return
    }

    $firstName = Split-Path ((Get-ShelfFile)[0]) -Leaf # the tile in slot 0
    $countBefore = Get-ShelfCount
    Hover-Notch; Show-ShelfPage
    $h = Get-NotchWindow
    Wait-Until { [NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 12, [IntPtr]::Zero, [IntPtr]::Zero) -ne -1 } 3000 | Out-Null
    $tile = [int][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 12, [IntPtr]::Zero, [IntPtr]::Zero)
    $r = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect($h, [ref]$r) | Out-Null
    $tx = $r.Left + ($tile -band 0xFFFF); $ty = $r.Top + (($tile -shr 16) -band 0xFFFF)
    [NotchTest]::SetCursorPos($tx, $ty) | Out-Null; Start-Sleep -Milliseconds 200
    [NotchDrag]::mouse_event([NotchDrag]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150
    for ($i = 1; $i -le 30; $i++) {
        [NotchTest]::SetCursorPos($tx + ($dx - $tx) * $i / 30, $ty + ($dy - $ty) * $i / 30) | Out-Null
        [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 25
    }
    for ($i = 0; $i -lt 10; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 30 }
    [NotchTest]::SetCursorPos($dx + 3, $dy + 3) | Out-Null
    for ($i = 0; $i -lt 10; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 30 }
    if ($form.IsDisposed -or -not [NotchDrag]::IsTopAt($form, $dx + 3, $dy + 3)) {
        Write-Host "  (drop window gone or covered mid-drag; events: $($events -join ' / '))"
        # Something covered our window mid-drag: cancel with Esc instead of dropping onto it.
        [NotchDrag]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero); [NotchDrag]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
    }
    [NotchDrag]::mouse_event([NotchDrag]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Wait-Until { $received.Count -gt 0 } 3000 | Out-Null
    # Compare names: the shell hands over long paths (C:\Users\Name) while $env:TEMP may be 8.3 (C:\Users\NAME~1).
    Assert (@($received | ForEach-Object { Split-Path $_ -Leaf }) -contains $firstName) "dragging the first tile out delivers $firstName to the other app (got: $($received -join ', '); events: $($events -join ' / '))"
    Assert (Wait-Until { (Get-ShelfCount) -eq $countBefore - 1 } 2000) "a dragged-out file leaves the shelf ($(Get-ShelfCount) left)"
    if (-not $form.IsDisposed) { $form.Close(); $form.Dispose() }

    # --- clear ---
    [NotchTest]::Command((Get-NotchWindow), $CMD.ClearShelf)
    Assert ((Get-ShelfCount) -eq 0) 'Clear empties the shelf'
    Assert (-not (Get-ShelfFile)) 'and the saved list'
} finally {
    Restore-Cursor $cur
    [NotchDrag]::mouse_event([NotchDrag]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
}
Finish-Test 'shelf'
