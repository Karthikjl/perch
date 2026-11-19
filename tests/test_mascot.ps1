# Mascot (a little friend that lives in the notch): after 15 s without input the closed
# pill turns into Perch's falcon (or the style chosen); 7 s later it falls asleep (still picture, no CPU); a real mouse move
# wakes it (eyes go wide), and 7 s later the clock is back. Mascot=0 turns it off. The appearance is
# started with a test message instead of waiting for 15 s with no input; the wake-up uses one tiny
# real mouse move (the pointer goes back). Uses its own temporary data folder.
$script:KeepMascot = $true
. "$PSScriptRoot\lib.ps1"
Write-Host '== Mascot =='
$work = Join-Path $env:TEMP "perch-mascot-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work

function Pet([int]$wp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 28, [IntPtr]$wp, [IntPtr]::Zero) }
function Mode { (Pet) -band 15 }   # 0 clock, 1 awake, 2 asleep, 3 waking (eyes go wide)
function Shown { (Pet) -shr 4 }    # percent of the face showing
function GlowColor { # the glow at the bottom of the pill, a little left of the middle (clear of the falcon's beak)
    $b = Get-Screen; $bmp = New-Object System.Drawing.Bitmap 1, 1; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $s = [int](([NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 17, [IntPtr]1, [IntPtr]::Zero).ToInt64() -shr 16) - 3)
    $g.CopyFromScreen([int]($b.Width / 2), $s, 0, 0, $bmp.Size); $c = $bmp.GetPixel(0, 0); $g.Dispose(); $bmp.Dispose(); $c
}
function Glow { $c = GlowColor; [math]::Max($c.R, [math]::Max($c.G, $c.B)) } # brightest channel (the falcon's glow is amber)

$cursor = Save-Cursor
& {
try {
    Start-Notch
    if ((Mode) -ne 0) { [NotchDrag]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero); Wait-Until { (Mode) -eq 0 } 9000 | Out-Null }
    Assert ((Mode) -eq 0) 'the clock pill shows while you are using the PC'

    Assert ((Pet 1) -eq 1) 'the falcon appears (as if you were away 15 s)'
    Assert (Wait-Until { (Shown) -eq 100 } 1500) 'it fades in'
    Start-Sleep -Milliseconds 600
    $awake = Glow
    Assert ($awake -gt 120) "the glow is on ($awake)"
    Save-TopShot 'mascot_awake' | Out-Null
    Assert (Wait-Until { (Mode) -eq 2 } 9000) 'it falls asleep after about 7 s'
    Start-Sleep -Seconds 3
    $asleep = Glow
    Assert ($asleep -lt $awake -and $asleep -gt 50) "asleep the glow dims ($awake -> $asleep)"
    Save-TopShot 'mascot_asleep' | Out-Null
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 4; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 30) "asleep it is a still picture: ~0 CPU ($([int]$cpu) ms over 4 s)"

    [NotchDrag]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero) # a real (tiny) mouse move
    Assert (Wait-Until { (Mode) -eq 3 } 1500) 'a mouse move wakes it (eyes go wide)'
    Start-Sleep -Milliseconds 1500
    Save-TopShot 'mascot_wake' | Out-Null
    Assert (Wait-Until { (Mode) -eq 0 } 9000) 'about 7 s later the clock is back'

    # Another style, with its own glow (Ember: warm). Saved in the file, it waits for the "Settings updated" pop-up.
    Set-Content -LiteralPath $ini -Value '[Notch]', 'Mascot=1', 'MascotStyle=Ember' -Encoding ASCII
    Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge 2 } 3000 | Out-Null
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Assert ((Pet 1) -eq 1 -and (Wait-Until { (Shown) -eq 100 } 1500)) 'MascotStyle=Ember: it appears'
    Start-Sleep -Milliseconds 600
    $c = GlowColor
    Assert ($c.R -gt 120 -and $c.R -gt $c.B + 40) "Ember's glow is warm (R $($c.R), B $($c.B))"
    Save-TopShot 'mascot_ember' | Out-Null
    [NotchDrag]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero)
    Assert (Wait-Until { (Mode) -eq 0 } 10000) 'it wakes and goes back to the clock'

    Set-Content -LiteralPath $ini -Value '[Notch]', 'Mascot=0' -Encoding ASCII
    Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge 3 } 3000 | Out-Null
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Assert ((Pet 1) -eq 0) 'Mascot=0: it never appears'
} finally {
    Restore-Cursor $cursor
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'mascot'
