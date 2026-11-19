# 3.4 Brightness pop-up: changing the built-in display's brightness shows a level bar.
# Changes your brightness by 10% for a moment, then restores it.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 3.4 Brightness pop-up =='
try { $wmi = Get-CimInstance -Namespace root/WMI -ClassName WmiMonitorBrightness -ErrorAction Stop | Select-Object -First 1 }
catch { Write-Host '  SKIP  no software brightness control (desktop monitor?)'; Finish-Test 'brightness' }
$methods = Get-CimInstance -Namespace root/WMI -ClassName WmiMonitorBrightnessMethods | Select-Object -First 1
function Set-Brightness([int]$v) { Invoke-CimMethod -InputObject $methods -MethodName WmiSetBrightness -Arguments @{ Timeout = 1; Brightness = [byte]$v } | Out-Null }
function Get-Brightness { (Get-CimInstance -Namespace root/WMI -ClassName WmiMonitorBrightness | Select-Object -First 1).CurrentBrightness }

Start-Notch
$h = Get-NotchWindow
$orig = Get-Brightness
Assert (Wait-Until { [NotchTest]::Brightness($h) -ge 0 } 3000) 'notch received the brightness from Windows'
Assert ([NotchTest]::Brightness($h) -eq $orig) "starting value matches ($([NotchTest]::Brightness($h))% vs $orig%)"

$target = if ($orig -gt 50) { $orig - 10 } else { $orig + 10 }
try {
    Set-Brightness $target
    Assert (Wait-Until { (Get-ToastText) -like 'Brightness*' } 3000) "brightness change shows a pop-up ('$(Get-ToastText)')"
    Assert (Wait-Until { [math]::Abs([NotchTest]::ToastLevel($h) - $target * 10) -le 10 } 2000) ("bar shows {0}% (expected {1}%)" -f ([NotchTest]::ToastLevel($h) / 10), $target)
    Start-Sleep -Milliseconds 350
    Save-TopShot 'brightness_toast' | Out-Null
} finally {
    Set-Brightness $orig
}
Assert (Wait-Until { (Get-Brightness) -eq $orig } 3000) "original brightness restored ($orig%)"
Finish-Test 'brightness'
