# Indicators and pop-ups: Caps Lock (real key presses, put back
# afterwards), input language, Wi-Fi connected / disconnected, Bluetooth headset connected, and
# the [Popups] switches that turn each off. Wi-Fi, Bluetooth and language are simulated with
# messages (the test can't switch your network, headset or keyboard language). Uses its own
# temporary data folder. Doesn't move the mouse.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Pop-ups: Caps Lock, input language, Wi-Fi, Bluetooth =='
$work = Join-Path $env:TEMP "perch-popups-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
$M = @{ Output = 6; Bt = 22; Wifi = 23; Lang = 24; BtCount = 25 }
function Send([int]$m, [int]$wp = 0) { [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + $m, [IntPtr]$wp, [IntPtr]::Zero) }
function Wait-Quiet { Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null; Start-Sleep -Milliseconds 200 }
function Press-Caps { [NotchTest]::keybd_event(0x14, 0x3A, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30; [NotchTest]::keybd_event(0x14, 0x3A, 2, [UIntPtr]::Zero) }
function Set-Ini([string[]]$lines) {
    $before = [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero)
    Set-Content -LiteralPath $ini -Value $lines -Encoding ASCII
    Wait-Until { [int][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -gt $before } 3000 | Out-Null
    Start-Sleep -Milliseconds 300; Wait-Quiet
}
$capsAtStart = [Console]::CapsLock

& {
try {
    Start-Notch
    if (Test-Flag Hidden) { Write-Host '  SKIP  a full screen app is in front, pop-ups are held back' -ForegroundColor Yellow; return }

    # --- Caps Lock: real key presses ---
    Press-Caps
    $want = if ([Console]::CapsLock) { 'On' } else { 'Off' }
    Assert (Wait-Until { (Get-ToastText) -eq "Caps Lock | $want" } 2000) "pressing Caps Lock shows it ($(Get-ToastText))"
    Wait-Quiet
    Press-Caps
    $want = if ([Console]::CapsLock) { 'On' } else { 'Off' }
    Assert (Wait-Until { (Get-ToastText) -eq "Caps Lock | $want" } 2000) "pressing it again shows the other state ($(Get-ToastText))"
    Assert ([Console]::CapsLock -eq $capsAtStart) 'Caps Lock is back as it was'
    Wait-Quiet

    # --- input language ---
    Send $M.Lang 0x0409 | Out-Null
    $dot = [char]0xB7
    Assert ((Get-ToastText) -eq "English (United States) | Input language $dot ENG") "English (US) ($(Get-ToastText))"
    Wait-Quiet
    Send $M.Lang 0x0439 | Out-Null
    Assert ((Get-ToastText) -like "Hindi*| Input language $dot HIN") "Hindi ($(Get-ToastText))"
    Wait-Quiet

    # --- Wi-Fi ---
    Send $M.Wifi 1 | Out-Null
    Assert ((Get-ToastText) -eq 'Connected | Test network') "Wi-Fi connected ($(Get-ToastText))"
    Wait-Quiet
    Send $M.Wifi 0 | Out-Null
    Assert ((Get-ToastText) -eq 'Disconnected | Test network') "Wi-Fi disconnected ($(Get-ToastText))"
    Wait-Quiet

    # --- Bluetooth headset ---
    Send $M.Bt | Out-Null
    Assert ((Get-ToastText) -eq 'Test Headset | Connected') "Bluetooth headset connected ($(Get-ToastText))"
    Send $M.Output | Out-Null # Windows then switches the default output to it
    Start-Sleep -Milliseconds 200
    Assert ((Get-ToastText) -eq 'Test Headset | Connected') "...the output-device pop-up doesn't cover it ($(Get-ToastText))"
    $bt = [int](Send $M.BtCount)
    Write-Host "  info  Bluetooth audio outputs Windows knows on this PC: $bt"
    Wait-Quiet

    # --- switches in [Popups] ---
    Set-Ini '[Popups]', 'CapsLock=0', 'InputLanguage=0', 'WiFi=0', 'Bluetooth=0'
    Press-Caps; Start-Sleep -Milliseconds 400; Press-Caps; Start-Sleep -Milliseconds 400
    Assert (-not (Test-Flag Toast)) 'CapsLock=0: no pop-up for Caps Lock'
    Assert ([Console]::CapsLock -eq $capsAtStart) '...and Caps Lock is back as it was'
    Send $M.Wifi 1 | Out-Null; Send $M.Bt | Out-Null; Start-Sleep -Milliseconds 300
    Assert (-not (Test-Flag Toast)) 'WiFi=0 and Bluetooth=0: no pop-ups for them'
    Set-Ini '[Popups]', 'Volume=0'
    Send $M.Output | Out-Null; Start-Sleep -Milliseconds 300
    Assert (-not (Test-Flag Toast)) 'Volume=0: no volume / output pop-up'
    Set-Ini '[Popups]', 'WiFi=maybe'
    Assert ((Get-ToastText) -like '*WiFi: "maybe" isn''t a number*' -or (Wait-Until { (Get-ToastText) -like '*WiFi: "maybe"*' } 3000)) "a bad value is reported ($(Get-ToastText))"

    # --- budget: the keyboard hook and Wi-Fi listener cost nothing while idle ---
    Set-Content -LiteralPath $ini -Value '[Popups]' -Encoding ASCII
    Wait-Until { Test-Flag Toast } 3000 | Out-Null # the reload's pop-up first, so the wait below isn't over before it starts
    Wait-Until { -not (Test-Flag Toast) -and -not (Test-Flag Expanded) } 7000 | Out-Null
    Start-Sleep -Seconds 2
    $proc = Get-Process Perch
    $cpu0 = $proc.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 5; $proc.Refresh()
    $cpu = $proc.TotalProcessorTime.TotalMilliseconds - $cpu0
    Assert ($cpu -lt 30) "idle CPU ~0 with the hook and Wi-Fi listener ($([int]$cpu) ms over 5 s)"
    Assert ($proc.WorkingSet64 -lt 13MB) "working set $([math]::Round($proc.WorkingSet64 / 1MB, 1)) MB < 13 MB"
} finally {
    if ([Console]::CapsLock -ne $capsAtStart) { Press-Caps }
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'popups'
