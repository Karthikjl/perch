# 3.1 / 3.2 Now playing + media controls.
# Plays a silent test track (with a pink cover) through Windows' MediaPlayer,
# which publishes it to the system media controls like Spotify or a browser would.
. "$PSScriptRoot\lib.ps1"
Write-Host '== 3.1/3.2 Now playing + controls =='

$null = [Windows.Media.Playback.MediaPlayer, Windows.Media.Playback, ContentType = WindowsRuntime]
$null = [Windows.Media.Core.MediaSource, Windows.Media.Core, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.RandomAccessStreamReference, Windows.Storage.Streams, ContentType = WindowsRuntime]
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
function Await($op, [type]$t) { $task = $asTask.MakeGenericMethod($t).Invoke($null, @($op)); $task.Wait(-1) | Out-Null; $task.Result }

# --- test assets: 60 s of silence + a 200x200 pink cover ---
$assets = Join-Path $env:TEMP 'notch-test'; New-Item -ItemType Directory -Force $assets | Out-Null
$wav = Join-Path $assets 'silence.wav'
if (-not (Test-Path $wav)) {
    $rate = 8000; $samples = $rate * 60; $data = $samples * 2
    $bw = New-Object IO.BinaryWriter([IO.File]::Create($wav))
    $bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $bw.Write([int](36 + $data)); $bw.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
    $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]1); $bw.Write([int]$rate); $bw.Write([int]($rate * 2)); $bw.Write([int16]2); $bw.Write([int16]16)
    $bw.Write([Text.Encoding]::ASCII.GetBytes('data')); $bw.Write([int]$data); $bw.Write((New-Object byte[] $data)); $bw.Close()
}
$png = Join-Path $assets 'cover.png'
$bmp = New-Object System.Drawing.Bitmap 200, 200
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(255, 45, 140)); $g.Dispose()
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

Start-Notch
$h = Get-NotchWindow

$player = New-Object Windows.Media.Playback.MediaPlayer
$cur = Save-Cursor
try {
    # Two-track playlist, so next/previous can be checked end to end.
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($png)) ([Windows.Storage.StorageFile])
    $list = New-Object Windows.Media.Playback.MediaPlaybackList
    # PowerShell can't call Add on a WinRT vector directly; go through ICollection<T>.
    $addItem = [System.Collections.Generic.ICollection[Windows.Media.Playback.MediaPlaybackItem]].GetMethod('Add')
    foreach ($title in 'Notch Test Song', 'Notch Test Song Two') {
        $item = New-Object Windows.Media.Playback.MediaPlaybackItem([Windows.Media.Core.MediaSource]::CreateFromUri([Uri]$wav))
        $props = $item.GetDisplayProperties()
        $props.Type = [Windows.Media.MediaPlaybackType]::Music
        $props.MusicProperties.Title = $title
        $props.MusicProperties.Artist = 'Test Artist'
        $props.Thumbnail = [Windows.Storage.Streams.RandomAccessStreamReference]::CreateFromFile($file)
        $item.ApplyDisplayProperties($props)
        $addItem.Invoke($list.Items, [object[]]@($item.psobject.BaseObject)) | Out-Null
    }
    $player.Source = $list
    $player.Play()

    Assert (Wait-Until { [NotchTest]::Title($h) -like '*Notch Test Song*' } 8000) "notch shows the track: '$([NotchTest]::Title($h))'"
    Assert ([NotchTest]::Title($h) -like '*Test Artist*') 'notch shows the artist'
    Assert (Wait-Until { Test-Flag Playing } 3000) 'notch knows it is playing'
    Assert (Wait-Until { Test-Flag Art } 3000) 'artwork loaded'
    Start-Sleep -Milliseconds 300
    Save-TopShot 'media_collapsed' | Out-Null

    Hover-Notch
    Wait-Until { Test-Flag Expanded } 2500 | Out-Null
    Start-Sleep -Milliseconds 300
    Save-TopShot 'media_expanded' | Out-Null
    Restore-Cursor $cur

    [NotchTest]::Command($h, $CMD.PlayPause)
    Assert (Wait-Until { $player.PlaybackSession.PlaybackState -eq 'Paused' } 3000) 'pause button pauses the player'
    Assert (Wait-Until { -not (Test-Flag Playing) } 3000) 'notch shows paused'
    [NotchTest]::Command($h, $CMD.PlayPause)
    Assert (Wait-Until { $player.PlaybackSession.PlaybackState -eq 'Playing' } 3000) 'play button resumes the player'
    Assert (Wait-Until { Test-Flag Playing } 3000) 'notch shows playing again'

    [NotchTest]::Command($h, $CMD.Next)
    Assert (Wait-Until { $list.CurrentItemIndex -eq 1 } 3000) 'next button skips to track 2'
    Assert (Wait-Until { [NotchTest]::Title($h) -like '*Notch Test Song Two*' } 4000) 'notch follows to track 2'
    [NotchTest]::Command($h, $CMD.Prev)
    Assert (Wait-Until { $list.CurrentItemIndex -eq 0 } 3000) 'previous button goes back to track 1'
    Assert (Wait-Until { [NotchTest]::Title($h) -like '*Notch Test Song -*' } 4000) 'notch follows back to track 1'
} finally {
    Restore-Cursor $cur
    $player.Pause(); $player.Source = $null; $player.Dispose()
}
Assert (Wait-Until { [NotchTest]::Title($h) -notlike '*Notch Test Song*' } 5000) 'track disappears when the player closes'
Finish-Test 'media'

