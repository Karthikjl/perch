# Notch style (Classic joined to the top edge / Floating just below it) and Glass (a blurred copy of what's
# behind, drawn inside the notch's outline).
# Uses its own temporary data folder (PERCH_DATA_DIR). Doesn't move the mouse.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Notch style and Glass =='
if (-not ('LookProbe' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LookProbe {
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    public struct RECT { public int Left, Top, Right, Bottom; }
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool GetWindowDisplayAffinity(IntPtr h, out uint a);
    public static uint GetWindowDisplayAffinity(IntPtr h) { uint a; GetWindowDisplayAffinity(h, out a); return a; }
    public static RECT[] BlurStrips() { // the visible Glass blur windows
        var list = new System.Collections.Generic.List<RECT>();
        EnumWindows((h, l) => { var sb = new System.Text.StringBuilder(64); GetClassName(h, sb, 64);
            RECT r; if (sb.ToString() == "PerchBlur" && IsWindowVisible(h) && GetWindowRect(h, out r)) list.Add(r); return true; }, IntPtr.Zero);
        return list.ToArray();
    }
}
'@
}

$work = Join-Path $env:TEMP "notch-look-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
$SQ = @{ Loads = 6; Style = 7; Glass = 8 }
function Get-Setting([int]$q) { [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]$q, [IntPtr]::Zero) }
function Write-Ini([string[]]$lines) { Set-Content -LiteralPath $ini -Value $lines -Encoding ASCII }
function Set-Look([string[]]$lines) { # write the file and wait until it has been applied
    $before = Get-Setting $SQ.Loads
    Write-Ini $lines
    Wait-Until { (Get-Setting $SQ.Loads) -gt $before } 3000 | Out-Null
    Start-Sleep -Milliseconds 400
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 4000 | Out-Null
    Start-Sleep -Milliseconds 300
}
function Get-Pill { # screen px of the pill as drawn
    $h = Get-NotchWindow
    $a = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 17, [IntPtr]0, [IntPtr]::Zero)
    $b = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 17, [IntPtr]1, [IntPtr]::Zero)
    [pscustomobject]@{ Left = [int]($a -band 0xFFFF); Top = [int](($a -shr 16) -band 0xFFFF); Right = [int]($b -band 0xFFFF); Bottom = [int](($b -shr 16) -band 0xFFFF) }
}
function Get-Pixel([int]$x, [int]$y) {
    $bmp = New-Object System.Drawing.Bitmap 1, 1
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
    $c = $bmp.GetPixel(0, 0); $g.Dispose(); $bmp.Dispose(); $c
}

& {
try {
    Start-Notch
    $scale = [LookProbe]::GetDpiForWindow((Get-NotchWindow)) / 96.0
    $scr = Get-Screen

    # --- defaults ---
    Assert ((Get-Setting $SQ.Style) -eq 0 -and (Get-Setting $SQ.Glass) -eq 0) 'defaults: Classic, no Glass'
    $p = Get-Pill
    Assert ($p.Top -eq $scr.Top -and [math]::Abs(($p.Left + $p.Right) / 2 - $scr.Width / 2) -le 2) "Classic: joined to the top edge, centred ($($p.Left),$($p.Top))"
    Assert ([LookProbe]::BlurStrips().Count -eq 0) 'no blur window without Glass'

    # --- Floating ---
    Set-Look '[Notch]', 'Style=Floating'
    $p = Get-Pill
    $gap = [math]::Round(8 * $scale)
    Assert ((Get-Setting $SQ.Style) -eq 1 -and $p.Top -eq $scr.Top + $gap) "Floating: $gap px below the top edge (top $($p.Top))"
    $inside = Get-Pixel ([int]($p.Left + 48 * $scale)) ([int](($p.Top + $p.Bottom) / 2)) # between the ring and the time
    Assert ($inside.R -le 10 -and $inside.G -le 10 -and $inside.B -le 10) "the pill is black ($($inside.R),$($inside.G),$($inside.B))"
    Write-Host "  screenshot: $(Save-TopShot 'look-floating')"

    # --- Glass ---
    Set-Look '[Notch]', 'Glass=1'
    Assert ((Get-Setting $SQ.Glass) -eq 1) 'Glass read from the file'
    Assert ((Get-Setting $SQ.Style) -eq 0) 'back to Classic when Style is left out'
    # The frost is Perch's own: a blurred copy of what's behind, drawn inside the notch's outline, so no
    # rectangle of Windows blur pokes out past the rounded corners, and no extra window is needed.
    Assert ([LookProbe]::BlurStrips().Count -eq 0) 'Glass needs no blur window (the blur is drawn inside the notch)'
    Assert ([LookProbe]::GetWindowDisplayAffinity((Get-NotchWindow)) -eq 0) "Perch still shows in screenshots after taking its copy (affinity $([LookProbe]::GetWindowDisplayAffinity((Get-NotchWindow))))"
    Write-Host "  screenshot: $(Save-TopShot 'look-glass')"
    Set-Look '[Notch]', 'Glass=0'
    Assert ([LookProbe]::BlurStrips().Count -eq 0) '...and none after turning it off'

    # --- bad values ---
    Write-Ini '[Notch]', 'Style=Round'
    Assert (Wait-Until { (Get-ToastText) -like '*Style: use Classic, Floating*' } 3000) "an unknown style is reported ($(Get-ToastText))"
    Write-Ini '[Notch]', 'Glass=maybe'
    Assert (Wait-Until { (Get-ToastText) -like '*Glass: "maybe" isn''t a number*' } 3000) "a bad Glass value is reported ($(Get-ToastText))"

    # --- budget ---
    Write-Ini '[Notch]'
    Start-Sleep -Milliseconds 2500
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 3; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 50) "idle CPU ~0 ($([int]$cpu) ms over 3 s)"
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    Stop-Notch
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'test_look'
