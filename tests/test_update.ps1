# Updates: Perch asks GitHub's releases API for the newest version, says when it's newer (and stays quiet
# when it isn't), refuses a download that isn't a Windows program, and installs a good one: the running
# Perch.exe is renamed, the new one takes its place, starts with --updated, and the old file is cleaned up.
# Runs a copy of Perch from a temporary folder against a fake GitHub on 127.0.0.1 (PERCH_UPDATE_URL),
# so build\Perch.exe and the internet are never touched.
$env:PERCH_UPDATE_URL = 'set below'
. "$PSScriptRoot\lib.ps1"
Write-Host '== Updates =='
$work = Join-Path $env:TEMP "perch-update-test-$PID"
$app = Join-Path $work 'app'
New-Item -ItemType Directory -Force $work, $app | Out-Null
$env:PERCH_DATA_DIR = $work
$copy = Join-Path $app 'Perch.exe'
Copy-Item $Exe $copy
$marker = [Text.Encoding]::ASCII.GetBytes('PERCH-TEST-UPDATE')
$newBytes = [IO.File]::ReadAllBytes($Exe) + $marker # still a valid program, with a mark to recognise it by

# ---- a fake GitHub ----
$sync = [hashtable]::Synchronized(@{ Tag = 'v0.1.0'; Exe = $newBytes; Requests = [System.Collections.ArrayList]::Synchronized((New-Object System.Collections.ArrayList)) })
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$sync.Port = $port
$server = [powershell]::Create()
$server.AddScript({
    param($l, $sync)
    while ($true) {
        try { $c = $l.AcceptTcpClient() } catch { break }
        try {
            $s = $c.GetStream(); $r = New-Object IO.StreamReader($s)
            $line = $r.ReadLine(); $ua = ''
            while (($h = $r.ReadLine()) -ne $null -and $h -ne '') { if ($h -match '^User-Agent:\s*(.+)$') { $ua = $Matches[1] } }
            $path = ($line -split ' ')[1]
            $sync.Requests.Add("$path | $ua") | Out-Null
            $type = 'application/json'
            if ($path -eq '/repos/x/releases/latest') {
                $b = [Text.Encoding]::UTF8.GetBytes('{"tag_name":"' + $sync.Tag + '","name":"Perch","assets":[{"name":"notes.txt","browser_download_url":"http://127.0.0.1:' + $sync.Port + '/dl/notes.txt"},{"name":"Perch.exe","browser_download_url":"http://127.0.0.1:' + $sync.Port + '/dl/Perch.exe"}]}')
            } elseif ($path -eq '/dl/Perch.exe') { $b = $sync.Exe; $type = 'application/octet-stream' }
            else { $b = [Text.Encoding]::UTF8.GetBytes('{"message":"Not Found"}') }
            $hd = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: $type`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n")
            $s.Write($hd, 0, $hd.Length); $s.Write($b, 0, $b.Length); $s.Flush()
        } catch {} finally { $c.Close() }
    }
}).AddArgument($listener).AddArgument($sync) | Out-Null
$serverRun = $server.BeginInvoke()
$env:PERCH_UPDATE_URL = "http://127.0.0.1:$port/repos/x/releases/latest"

function Upd([int]$wp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 40, [IntPtr]$wp, [IntPtr]::Zero) } # 1 checking, 2 up to date, 3 available, 4 installing, 5 failed, 6 offline
function Start-Copy {
    Get-Process Perch -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 300
    Start-Process $copy | Out-Null
    if (-not (Wait-Until { (Get-NotchWindow) -ne [IntPtr]::Zero } 5000)) { throw 'Perch did not start' }
    Start-Sleep -Milliseconds 500
}

& {
try {
    Start-Copy
    Upd 1 | Out-Null
    Assert (Wait-Until { (Upd) -eq 2 } 5000) "same version on GitHub: up to date (status $(Upd))"
    Assert ($sync.Requests[0] -like '/repos/x/releases/latest | Perch-updater*') 'asked GitHub for the latest release, as Perch-updater'

    $sync.Tag = 'v9.9.9'
    Upd 1 | Out-Null
    Assert (Wait-Until { (Upd) -eq 3 } 5000) "a newer version is offered (status $(Upd))"

    # a download that isn't a Windows program is refused, and nothing changes
    $sync.Exe = [Text.Encoding]::ASCII.GetBytes('<html>not a program</html>' * 4000)
    Upd 2 | Out-Null
    Assert (Wait-Until { (Upd) -eq 5 } 8000) "a broken download is refused (status $(Upd))"
    Assert (((Get-FileHash $copy).Hash -eq (Get-FileHash $Exe).Hash) -and -not (Test-Path "$copy.new") -and -not (Test-Path "$copy.old")) '...and Perch.exe is left as it was'
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null

    # a good one is installed
    $sync.Exe = $newBytes
    Upd 1 | Out-Null
    Wait-Until { (Upd) -eq 3 } 5000 | Out-Null
    $oldPid = (Get-Process Perch).Id
    Upd 2 | Out-Null
    Assert (Wait-Until { -not (Get-Process -Id $oldPid -ErrorAction SilentlyContinue) } 10000) 'the old Perch quits'
    Assert (Wait-Until { (Get-NotchWindow) -ne [IntPtr]::Zero } 8000) 'the new Perch starts'
    $new = Get-CimInstance Win32_Process -Filter "Name = 'Perch.exe'" | Select-Object -First 1
    Assert ($new.ExecutablePath -eq $copy -and $new.CommandLine -like '*--updated*') "...from the same place, told it was updated ($($new.CommandLine))"
    $bytes = [IO.File]::ReadAllBytes($copy)
    Assert ([Text.Encoding]::ASCII.GetString($bytes, $bytes.Length - $marker.Length, $marker.Length) -eq 'PERCH-TEST-UPDATE') 'Perch.exe is now the downloaded one'
    Assert (Wait-Until { -not (Test-Path "$copy.old") -and -not (Test-Path "$copy.new") } 5000) 'the old file is cleaned up'
    Assert (Wait-Until { (Get-ToastText) -like 'Perch updated*' } 3000) "it says it was updated ($(Get-ToastText))"
} finally {
    Get-Process Perch -ErrorAction SilentlyContinue | Stop-Process -Force
    try { $listener.Stop() } catch {}
    $server.Dispose()
    Remove-Item Env:\PERCH_DATA_DIR, Env:\PERCH_UPDATE_URL -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'update'
