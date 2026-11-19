# 3.5 Battery pop-ups.
# Part 1 (real): the notch receives Windows' battery reports and they match the system.
# Part 2 (simulated): plug/unplug, low battery and fully charged pop-ups. A test can't
# pull the charger, so these reports are injected through the same code path.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 3.5 Battery pop-ups =='
$ps = [System.Windows.Forms.SystemInformation]::PowerStatus
if ($ps.BatteryChargeStatus -band [System.Windows.Forms.BatteryChargeStatus]::NoSystemBattery) { Write-Host '  SKIP  no battery'; Finish-Test 'battery' }
Start-Notch
$h = Get-NotchWindow

# --- real reports from Windows ---
Assert (Wait-Until { [NotchTest]::Battery($h) -ge 0 } 3000) 'notch received a battery report from Windows'
$b = [NotchTest]::Battery($h)
$pct = $b -band 0xFF; $ac = ($b -shr 8) -eq 1
$sysPct = [math]::Round($ps.BatteryLifePercent * 100); $sysAC = $ps.PowerLineStatus -eq 'Online'
Assert ([math]::Abs($pct - $sysPct) -le 1) "battery level matches Windows ($pct% vs $sysPct%)"
Assert ($ac -eq $sysAC) "charger state matches Windows (plugged in: $ac)"
Assert (-not (Test-Flag Toast)) 'no pop-up for the starting values'

# --- simulated changes ---
try {
    [NotchTest]::SimPower($h, 1, 60); [NotchTest]::SimPower($h, 0, -1)
    Assert ((Get-ToastText) -eq 'On battery | 60% remaining') "unplug shows 'On battery' ('$(Get-ToastText)')"
    [NotchTest]::SimPower($h, 1, -1)
    Assert ((Get-ToastText) -like 'Charging | 60%*plugged in') "plug in shows 'Charging' ('$(Get-ToastText)')"
    Start-Sleep -Milliseconds 350
    Save-TopShot 'battery_charging' | Out-Null
    [NotchTest]::SimPower($h, 0, 21); [NotchTest]::SimPower($h, -1, 20)
    Assert ((Get-ToastText) -like 'Low battery | 20% left*') "dropping to 20% on battery warns ('$(Get-ToastText)')"
    Start-Sleep -Milliseconds 350
    Save-TopShot 'battery_low' | Out-Null
    [NotchTest]::SimPower($h, 1, 99); [NotchTest]::SimPower($h, -1, 100)
    Assert ((Get-ToastText) -like 'Fully charged*') "reaching 100% on the charger says so ('$(Get-ToastText)')"
    $before = Get-ToastText; [NotchTest]::SimPower($h, -1, 98)
    Assert ((Get-ToastText) -eq $before) 'ordinary level changes stay quiet'
} finally {
    Start-Notch # restart so the notch goes back to the real battery state
}
Finish-Test 'battery'
