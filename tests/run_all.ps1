# Builds build\Perch.exe and runs every feature test. Usage: powershell -STA -File tests\run_all.ps1
$root = Split-Path $PSScriptRoot -Parent
Write-Host '== Build =='
Get-Process Perch -ErrorAction SilentlyContinue | Stop-Process -Force  # a running copy locks Perch.exe
Start-Sleep -Milliseconds 300
cmd /c "`"$root\build.bat`""
if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED' -ForegroundColor Red; exit 1 }

$tests = 'test_core', 'test_autostart', 'test_fullscreen', 'test_media', 'test_volume', 'test_output_device', 'test_battery', 'test_brightness', 'test_shelf', 'test_settings', 'test_look', 'test_pages', 'test_closed', 'test_popups', 'test_pomodoro', 'test_todo', 'test_mascot', 'test_calendar', 'test_water', 'test_clipboard'
$results = foreach ($t in $tests) {
    & powershell -NoProfile -STA -ExecutionPolicy Bypass -File "$PSScriptRoot\$t.ps1"
    [pscustomobject]@{ Test = $t; Result = if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' } }
}
Write-Host ''
$results | Format-Table -AutoSize
# Leave a fresh copy running for the user.
Start-Process "$root\build\Perch.exe"
if ($results.Result -contains 'FAIL') { exit 1 }
