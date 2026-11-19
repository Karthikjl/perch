# 1.4 Start with Windows toggles the HKCU Run entry. Restores the original state.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 1.4 Start with Windows =='
Start-Notch
$h = Get-NotchWindow
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
function Get-RunValue { (Get-ItemProperty $runKey -Name Perch -ErrorAction SilentlyContinue).Perch }
$before = Test-Flag Autostart
try {
    [NotchTest]::Command($h, $CMD.Autostart)
    Assert ((Test-Flag Autostart) -ne $before) 'menu command toggles autostart'
    if (-not $before) {
        Assert ((Get-RunValue) -eq "`"$Exe`"") "Run entry points at this exe: $(Get-RunValue)"
    }
    [NotchTest]::Command($h, $CMD.Autostart)
    Assert ((Test-Flag Autostart) -eq $before) 'toggling again restores it'
    if (-not $before) { Assert ($null -eq (Get-RunValue)) 'Run entry removed' }
} finally {
    if ((Test-Flag Autostart) -ne $before) { [NotchTest]::Command($h, $CMD.Autostart) }
}
Finish-Test 'autostart'
