# 3.6 Output-device pop-up: switching the default output (e.g. plugging in headphones)
# shows the new device. Switches to another output device and back; skipped with only one.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 3.6 Output device pop-up =='
$ids = [NotchAudio]::OutputIds()
if ($ids.Count -lt 2) { Write-Host '  SKIP  only one output device'; Finish-Test 'output_device' }
Start-Notch
$roles = 0, 1, 2  # console, multimedia, communications
$orig = @{}; foreach ($r in $roles) { $orig[$r] = [NotchAudio]::DefaultOutputId($r) }
$other = $ids | Where-Object { $_ -ne $orig[0] } | Select-Object -First 1
try {
    $before = Get-ToastText
    foreach ($r in $roles) { [NotchAudio]::SetDefaultOutput($other, $r) }
    Assert (Wait-Until { (Get-ToastText) -ne $before -and (Get-ToastText) -match '^(Headphones|Sound output) \| ' } 4000) "switching output shows a pop-up ('$(Get-ToastText)')"
    $first = Get-ToastText
    Start-Sleep -Milliseconds 350
    Save-TopShot 'output_device' | Out-Null
} finally {
    foreach ($r in $roles) { if ($orig[$r]) { [NotchAudio]::SetDefaultOutput($orig[$r], $r) } }
}
Assert (Wait-Until { (Get-ToastText) -ne $first -and (Get-ToastText) -match '^(Headphones|Sound output) \| ' } 4000) "switching back shows the original device ('$(Get-ToastText)')"
Assert ([NotchAudio]::DefaultOutputId(0) -eq $orig[0]) 'original default output restored'
Finish-Test 'output_device'
