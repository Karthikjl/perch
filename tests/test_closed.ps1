# Closed pill: the status ring on the left and the time, bold, in the middle; its size
# (150 x 30 DIPs); Notch size Auto/100-150 %; Minimal notch; Clock and Ring settings; the clock
# redraws at the minute. Uses its own temporary data folder (PERCH_DATA_DIR). Doesn't move the mouse;
# reads pixels of the closed pill, so it skips them while the pill is open or hidden.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Closed pill =='
if (-not ('ClosedProbe' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClosedProbe {
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
}
'@
}

$work = Join-Path $env:TEMP "notch-closed-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
$SQ = @{ Width = 2; Height = 3; Loads = 6; Size = 9; Minimal = 10; Clock = 11; Ring = 12; Scale = 13 }
function Get-Setting([int]$q) { [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]$q, [IntPtr]::Zero) }
function Write-Ini([string[]]$lines) { Set-Content -LiteralPath $ini -Value $lines -Encoding ASCII }
function Set-Ini([string[]]$lines) { # write the file and wait until it has been applied and the pill is closed
    $before = Get-Setting $SQ.Loads
    Write-Ini $lines
    Wait-Until { (Get-Setting $SQ.Loads) -gt $before } 3000 | Out-Null
    Start-Sleep -Milliseconds 400
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 5000 | Out-Null
    Start-Sleep -Milliseconds 400
}
function Get-Pill {
    $h = Get-NotchWindow
    $a = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 17, [IntPtr]0, [IntPtr]::Zero)
    $b = [int64][NotchTest]::SendMessage($h, [NotchTest]::WM_APP + 17, [IntPtr]1, [IntPtr]::Zero)
    [pscustomobject]@{ Left = [int]($a -band 0xFFFF); Top = [int](($a -shr 16) -band 0xFFFF); Right = [int]($b -band 0xFFFF); Bottom = [int](($b -shr 16) -band 0xFFFF) }
}
# Pixels of a screen rectangle: count of bright (text) and green (ring) pixels.
function Measure-Area([int]$x, [int]$y, [int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size); $g.Dispose()
    $bright = 0; $green = 0; $sig = New-Object System.Text.StringBuilder
    for ($j = 0; $j -lt $h; $j++) { for ($i = 0; $i -lt $w; $i++) {
        $c = $bmp.GetPixel($i, $j)
        if ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 180) { $bright++; [void]$sig.Append("$i,$j;") }
        if ($c.G -gt 140 -and $c.R -lt 120 -and $c.B -lt 140) { $green++ }
    } }
    $bmp.Dispose()
    [pscustomobject]@{ Bright = $bright; Green = $green; Sig = $sig.ToString() }
}
function Pill-Ready { -not (Test-Flag Expanded) -and -not (Test-Flag Hidden) -and -not (Test-Flag Playing) }

& {
try {
    Start-Notch
    $h = Get-NotchWindow
    $dpi = [ClosedProbe]::GetDpiForWindow($h) / 96.0

    # --- defaults ---
    Assert ((Get-Setting $SQ.Width) -eq 150 -and (Get-Setting $SQ.Height) -eq 30) 'closed pill is 150 x 30 DIPs'
    Assert ((Get-Setting $SQ.Size) -eq 0 -and (Get-Setting $SQ.Minimal) -eq 0 -and (Get-Setting $SQ.Clock) -eq 1 -and (Get-Setting $SQ.Ring) -eq 0) 'defaults: size Auto, Minimal off, clock on, battery ring'
    Assert ((Get-Setting $SQ.Scale) -eq [math]::Round($dpi * 1000)) "Auto size follows Windows scaling ($((Get-Setting $SQ.Scale) / 10) %)"

    # A known battery state (on battery, 60%), so the ring shows on desktops too.
    [NotchTest]::SimPower($h, 0, 60)
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 6000 | Out-Null
    Start-Sleep -Milliseconds 400

    if (Pill-Ready) {
        $p = Get-Pill
        $s = ($p.Right - $p.Left) / 150.0
        Assert ([math]::Abs($s - $dpi) -lt 0.03) "drawn pill is 150 DIPs wide ($($p.Right - $p.Left) px)"
        $cy = [int](($p.Top + $p.Bottom) / 2)
        $ring = Measure-Area ([int]($p.Left + 18 * $s)) ($cy - [int](9 * $s)) ([int](18 * $s)) ([int](18 * $s))
        Assert ($ring.Green -gt 10) "a green battery ring on the left ($($ring.Green) green px)"
        $time = Measure-Area ([int]($p.Left + 40 * $s)) ($cy - [int](8 * $s)) ([int](72 * $s)) ([int](16 * $s))
        Assert ($time.Bright -gt 20) "the time in the middle ($($time.Bright) white px)"
        Write-Host "  screenshot: $(Save-TopShot 'closed-default')"

        # the clock redraws when the minute changes (waits for the next minute, up to ~62 s)
        $m = (Get-Date).Minute
        Wait-Until { (Get-Date).Minute -ne $m } 62000 | Out-Null
        Start-Sleep -Milliseconds 600
        $after = Measure-Area ([int]($p.Left + 40 * $s)) ($cy - [int](8 * $s)) ([int](72 * $s)) ([int](16 * $s))
        if (Pill-Ready) { Assert ($after.Sig -ne $time.Sig) 'the time on the pill changes at the next minute' }
        else { Write-Host '  SKIP  minute redraw (the pill opened or hid meanwhile)' -ForegroundColor Yellow }

        # --- Clock off / Ring None ---
        Set-Ini '[Notch]', 'Clock=0'
        $t = Measure-Area ([int]($p.Left + 40 * $s)) ($cy - [int](8 * $s)) ([int](72 * $s)) ([int](16 * $s))
        Assert ((Get-Setting $SQ.Clock) -eq 0 -and $t.Bright -eq 0) "Clock=0 hides the time ($($t.Bright) white px)"
        Set-Ini '[Notch]', 'Ring=None'
        $r = Measure-Area ([int]($p.Left + 18 * $s)) ($cy - [int](9 * $s)) ([int](18 * $s)) ([int](18 * $s))
        Assert ((Get-Setting $SQ.Ring) -eq 1 -and $r.Green -eq 0) "Ring=None hides the ring ($($r.Green) green px)"

        # --- Minimal: a 44 x 4 tab with no clock while nothing is playing ---
        Set-Ini '[Notch]', 'Minimal=1'
        Start-Sleep -Milliseconds 900 # the pill springs down to the tab
        if (Pill-Ready) {
            $q = Get-Pill
            $mw = [math]::Round(($q.Right - $q.Left) / $s, 1); $mh = [math]::Round(($q.Bottom - $q.Top) / $s, 1)
            Assert ((Get-Setting $SQ.Minimal) -eq 1 -and [math]::Abs($mw - 44) -le 1 -and [math]::Abs($mh - 4) -le 1) "Minimal: a 44 x 4 tab while nothing plays ($mw x $mh)"
            $all = Measure-Area ([int]$q.Left) ([int]$q.Top) ([int]($q.Right - $q.Left)) ([int][math]::Max(1, $q.Bottom - $q.Top))
            Assert ($all.Green -eq 0 -and $all.Bright -eq 0) "...with nothing drawn on it ($($all.Green) green, $($all.Bright) white px)"
            Write-Host "  screenshot: $(Save-TopShot 'closed-minimal')"
            [NotchTest]::Command($h, 115) # right-click > Minimal notch: off
            Assert (Wait-Until { (Get-Setting $SQ.Minimal) -eq 0 -and (Get-Content $ini) -contains 'Minimal=0' } 2000) 'the Minimal notch menu item turns Minimal off and saves it'
            Start-Sleep -Milliseconds 900
            $q = Get-Pill
            Assert ([math]::Abs(($q.Right - $q.Left) / $s - 150) -le 1) "...and the pill springs back to 150 wide ($([math]::Round(($q.Right - $q.Left) / $s, 1)))"
            [NotchTest]::Command($h, 115)
            Assert (Wait-Until { (Get-Setting $SQ.Minimal) -eq 1 } 2000) '...and back on'
        } else { Write-Host '  SKIP  Minimal tab (media started playing or the pill opened meanwhile)' -ForegroundColor Yellow }
    } else {
        Write-Host '  SKIP  pixel checks (the pill is open, hidden for a full screen app, or media is playing)' -ForegroundColor Yellow
    }

    # --- Notch size ---
    Set-Ini '[Notch]', 'Size=150%'
    $k = (Get-Setting $SQ.Scale) / 1000.0
    Assert ((Get-Setting $SQ.Size) -eq 4 -and [math]::Abs($k - $dpi * 1.5) -lt 0.02) "Size=150% scales the notch by 1.5 ($k px per DIP)"
    $wr = New-Object NotchTest+RECT; [NotchTest]::GetWindowRect((Get-NotchWindow), [ref]$wr) | Out-Null
    Assert ([math]::Abs(($wr.Right - $wr.Left) - [math]::Ceiling(440 * $k)) -le 1) "...the window grows with it ($($wr.Right - $wr.Left) px)"
    $p = Get-Pill
    if (Test-Flag Expanded) { Write-Host '  SKIP  closed pill size at 150% (the pointer is over the notch)' -ForegroundColor Yellow }
    else { Assert ([math]::Abs(($p.Right - $p.Left) - 150 * $k) -le 2 -and [math]::Abs(($p.Left + $p.Right) / 2 - (Get-Screen).Width / 2) -le 2) "...and the pill is 1.5x as big and still centred ($($p.Right - $p.Left) px)" }
    Write-Host "  screenshot: $(Save-TopShot 'closed-150')"
    Set-Ini '[Notch]', 'Size=115%'
    Assert ([math]::Abs((Get-Setting $SQ.Scale) / 1000.0 - $dpi * 1.15) -lt 0.02) 'Size=115% scales by 1.15'
    Set-Ini '[Notch]'
    Assert ((Get-Setting $SQ.Scale) -eq [math]::Round($dpi * 1000)) 'removing Size goes back to Auto'

    # --- bad values ---
    Write-Ini '[Notch]', 'Size=200%'
    Assert (Wait-Until { (Get-ToastText) -like '*Size: use Auto, 100%, 115%, 130%, 150%*' } 3000) "an unknown size is reported ($(Get-ToastText))"
    Write-Ini '[Notch]', 'Ring=Moon'
    Assert (Wait-Until { (Get-ToastText) -like '*Ring: use Battery, None*' } 3000) "an unknown ring is reported ($(Get-ToastText))"

    # --- budget: the minute timer costs nothing ---
    Write-Ini '[Notch]'
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 6000 | Out-Null
    Start-Sleep -Seconds 2
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    if (Test-Flag Playing) { Write-Host "  SKIP  idle CPU: media is playing, so the sound bars' wave is moving ($([int]$cpu) ms over 5 s)" -ForegroundColor Yellow }
    else { Assert ($cpu -lt 30) "idle CPU ~0 with the clock on ($([int]$cpu) ms over 5 s)" }
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'closed'
