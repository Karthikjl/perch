# Clipboard history: copied text is kept (newest first, a repeat moves to the top, blank copies ignored),
# copies marked private (ExcludeClipboardContentFromMonitorProcessing) are skipped, Max caps the list,
# the Clipboard tab copies an item back and removes one, Remember keeps it encrypted across a restart,
# and turning it off forgets everything. Changes the real clipboard (puts your text back) and uses its
# own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Clipboard history =='
Add-Type -AssemblyName System.Windows.Forms
if (-not ('ClipProbe' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ClipProbe {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
}
$work = Join-Path $env:TEMP "perch-clip-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$dat = Join-Path $work 'clipboard.dat'
$env:PERCH_DATA_DIR = $work
function Ini([string[]]$lines) { Set-Content -LiteralPath $ini -Value (@('[Clipboard]') + $lines) -Encoding ASCII }
Ini 'History=1', 'Max=5', 'Remember=0'
function Count { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 34, [IntPtr]::Zero, [IntPtr]::Zero) }
function Set-Clip($data) { # retries while another app has the clipboard open
    for ($try = 0; $try -lt 10; $try++) { try { [System.Windows.Forms.Clipboard]::SetDataObject($data, $true, 10, 50); return } catch { Start-Sleep -Milliseconds 200 } }
    throw 'the clipboard stayed busy'
}
function Copy-Text([string]$t) { Set-Clip $t; Start-Sleep -Milliseconds 250 }
function Copy-Private([string]$t) {
    $d = New-Object System.Windows.Forms.DataObject
    $d.SetData([System.Windows.Forms.DataFormats]::UnicodeText, $t)
    $d.SetData('ExcludeClipboardContentFromMonitorProcessing', (New-Object byte[] 4))
    Set-Clip $d; Start-Sleep -Milliseconds 250
}
function Hot-Client([int]$hot) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 20, [IntPtr]$hot, [IntPtr]::Zero) }
function Tap([int]$hot) { [ClipProbe]::PostMessage((Get-NotchWindow), 0x202, [IntPtr]0, [IntPtr](Hot-Client $hot)) | Out-Null; Start-Sleep -Milliseconds 250 }
function Wait-Loads([int]$n) { Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge $n } 3000 | Out-Null; Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null }

$saved = $null
try { if ([System.Windows.Forms.Clipboard]::ContainsText()) { $saved = [System.Windows.Forms.Clipboard]::GetText() } } catch {}
$cursor = Save-Cursor
& {
try {
    Start-Notch
    Assert ((Count) -eq 0) 'nothing kept yet'
    Copy-Text 'alpha'; Copy-Text 'beta'
    Assert (Wait-Until { (Count) -eq 2 } 2000) 'two copies are kept'
    Copy-Text 'alpha'
    Assert ((Count) -eq 2) 'copying the same text again moves it up, no duplicate'
    Copy-Private 'hunter2'
    Assert ((Count) -eq 2) 'a copy marked private (password managers) is skipped'
    Copy-Text '   '
    Assert ((Count) -eq 2) 'blank text is ignored'
    foreach ($i in 1..5) { Copy-Text "item $i" }
    Assert (Wait-Until { (Count) -eq 5 } 2000) "Max=5 keeps the newest 5 ($(Count))"

    # the Clipboard tab: copy an item back, remove one
    [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 34, [IntPtr]1, [IntPtr]1) | Out-Null
    [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 19, [IntPtr]2, [IntPtr]::Zero) | Out-Null
    if (Wait-Until { Hover-Notch; Test-Flag Expanded } 3000) {
        [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), 110) | Out-Null; Start-Sleep -Milliseconds 500
        Write-Host "  screenshot: $(Save-TopShot 'clipboard-tab')"
        Tap 281 # second row
        Assert ([System.Windows.Forms.Clipboard]::GetText() -eq 'item 4') "clicking a row copies it back ($([System.Windows.Forms.Clipboard]::GetText()))"
        Assert ((Count) -eq 5) '...without adding it again'
        Tap 290 # first row's x
        Assert ((Count) -eq 4) 'the x removes an item'
    } else { Write-Host '  SKIP  the notch did not open (screen locked or covered?)' -ForegroundColor Yellow }
    [NotchTest]::SetCursorPos([int]((Get-Screen).Width / 2), [int]((Get-Screen).Height / 2)) | Out-Null
    Wait-Until { -not (Test-Flag Expanded) } 3000 | Out-Null
    $n = Count

    # Remember: saved encrypted, back after a restart
    Ini 'History=1', 'Max=5', 'Remember=1'
    Wait-Loads 2
    Assert (Test-Path $dat) 'Remember=1 saves clipboard.dat'
    $bytes = [IO.File]::ReadAllBytes($dat)
    $plain = [Text.Encoding]::Unicode.GetString($bytes) + [Text.Encoding]::UTF8.GetString($bytes)
    Assert (-not ($plain -match 'item')) '...encrypted (no copied text readable in the file)'
    Stop-Notch; Start-Sleep -Milliseconds 300; Start-Notch
    Assert ((Count) -eq $n) "after a restart the list is back ($(Count) of $n)"
    Ini 'History=1', 'Max=5', 'Remember=0'
    Wait-Loads 2
    Assert (-not (Test-Path $dat)) 'Remember=0 deletes clipboard.dat'

    # off: forgets and stops listening
    Ini 'History=0'
    Wait-Loads 3
    Assert ((Count) -eq 0) 'History=0 forgets the list'
    Copy-Text 'after'
    Assert ((Count) -eq 0) '...and stops keeping copies'

    $p = Get-Process Perch
    $c0 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 4; $p.Refresh()
    Assert (($p.TotalProcessorTime.TotalMilliseconds - $c0) -lt 30) "idle CPU ~0 ($([int]($p.TotalProcessorTime.TotalMilliseconds - $c0)) ms over 4 s)"
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    try { if ($null -ne $saved) { [System.Windows.Forms.Clipboard]::SetText($saved) } else { [System.Windows.Forms.Clipboard]::Clear() } } catch {}
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'clipboard'
