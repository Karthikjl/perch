# 4.1 The notch steps aside while a fullscreen app is in front, then returns.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 4.1 Fullscreen step-aside =='
Start-Notch
$h = Get-NotchWindow
Assert (-not (Test-Flag FullscreenHidden)) 'visible before fullscreen'

$b = Get-Screen
$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = 'None'
$form.StartPosition = 'Manual'
$form.Bounds = $b
$form.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 60)
$form.Text = 'Notch fullscreen test'
try {
    $form.Show()
    # Tap Alt so Windows lets this process take the foreground.
    [NotchTest]::keybd_event($VK.Alt, 0, 0, [UIntPtr]::Zero); [NotchTest]::keybd_event($VK.Alt, 0, 2, [UIntPtr]::Zero)
    [NotchTest]::SetForegroundWindow($form.Handle) | Out-Null
    $form.Activate()
    Assert (Wait-Until { Test-Flag FullscreenHidden } 5000) 'notch hides for the fullscreen window'
    Assert (-not [NotchTest]::IsWindowVisible($h)) 'notch window is actually hidden'
} finally { $form.Close(); $form.Dispose() }
Assert (Wait-Until { -not (Test-Flag FullscreenHidden) } 5000) 'notch comes back after fullscreen closes'
Assert ([NotchTest]::IsWindowVisible($h)) 'notch window is visible again'
Finish-Test 'fullscreen'
