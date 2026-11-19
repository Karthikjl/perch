# 3.3 Volume pop-up: changing the system volume shows a level bar; muting shows "Muted".
# Briefly changes your volume (by 13%) and mute, then restores both.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 3.3 Volume pop-up =='
Start-Notch
$h = Get-NotchWindow
$origVol = [NotchAudio]::GetVolume()
$origMute = [NotchAudio]::GetMute()
$target = if ($origVol -gt 0.5) { [math]::Round($origVol - 0.13, 2) } else { [math]::Round($origVol + 0.13, 2) }
try {
    [NotchAudio]::SetVolume($target)
    Assert (Wait-Until { (Get-ToastText) -like 'Volume*' } 2000) "volume change shows a pop-up ('$(Get-ToastText)')"
    $lvl = [NotchTest]::ToastLevel($h)
    Assert ([math]::Abs($lvl - $target * 1000) -le 5) ("bar shows the new level: {0:N0}% (expected {1:N0}%)" -f ($lvl / 10), ($target * 100))
    Start-Sleep -Milliseconds 350
    Save-TopShot 'volume_toast' | Out-Null

    [NotchAudio]::SetMute($true)
    Assert (Wait-Until { (Get-ToastText) -like 'Muted*' } 2000) 'muting shows "Muted"'
    Start-Sleep -Milliseconds 350
    Save-TopShot 'volume_muted' | Out-Null
} finally {
    [NotchAudio]::SetMute($origMute)
    [NotchAudio]::SetVolume($origVol)
}
Assert ([math]::Abs([NotchAudio]::GetVolume() - $origVol) -lt 0.005) 'original volume restored'
Finish-Test 'volume'
