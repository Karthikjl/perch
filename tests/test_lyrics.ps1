# Synced lyrics: nothing is looked up while Lyrics is off; with it on, a song is looked up on LRCLIB
# (title, artist and length in the address), its timed lines are read ([mm:ss.xx], several times on one
# line, tags skipped), the line at a given moment is found (Earlier shifts it), a song with no exact match
# falls back to a search, and a song with no synced lyrics says so. A fake LRCLIB on 127.0.0.1 stands in
# (PERCH_LRCLIB_URL), so nothing goes over the internet. Songs are given by a test message, not a player.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Lyrics =='
if (-not ('LyProbe' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class LyProbe {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern ushort GlobalAddAtom(string s);
    [DllImport("kernel32.dll")] public static extern ushort GlobalDeleteAtom(ushort a);
}
'@
}
$work = Join-Path $env:TEMP "perch-lyrics-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work

# ---- a fake LRCLIB ----
$sync = [hashtable]::Synchronized(@{ Requests = [System.Collections.ArrayList]::Synchronized((New-Object System.Collections.ArrayList)) })
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$server = [powershell]::Create()
$server.AddScript({
    param($l, $sync)
    while ($true) {
        try { $c = $l.AcceptTcpClient() } catch { break }
        try {
            $s = $c.GetStream(); $r = New-Object IO.StreamReader($s)
            $line = $r.ReadLine(); while (($h = $r.ReadLine()) -ne $null -and $h -ne '') {}
            $path = ($line -split ' ')[1]
            $sync.Requests.Add($path) | Out-Null
            $code = '200 OK'
            if ($path.StartsWith('/api/get?track_name=Song%20One&artist_name=The%20Band')) {
                $out = '{"id":1,"trackName":"Song One","plainLyrics":"First line","syncedLyrics":"[ar:The Band]\n[00:01.00]First line\n[00:05.50] Second line \n[00:09.00][00:12.00]Chorus\n"}'
            } elseif ($path.StartsWith('/api/search?track_name=Other')) {
                $out = '[{"id":2,"syncedLyrics":null},{"id":3,"syncedLyrics":"[00:02.00]Found by search"}]'
            } elseif ($path.StartsWith('/api/search')) { $out = '[]' }
            else { $code = '404 Not Found'; $out = '{"code":404,"name":"TrackNotFound","message":"Failed to find specified track"}' }
            $b = [Text.Encoding]::UTF8.GetBytes($out)
            $hd = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $code`r`nContent-Type: application/json`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n")
            $s.Write($hd, 0, $hd.Length); $s.Write($b, 0, $b.Length); $s.Flush()
        } catch {} finally { $c.Close() }
    }
}).AddArgument($listener).AddArgument($sync) | Out-Null
$serverRun = $server.BeginInvoke()
$env:PERCH_LRCLIB_URL = "http://127.0.0.1:$port/api"

function Ly([int]$wp = 0, [int]$lp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 38, [IntPtr]$wp, [IntPtr]$lp) }
function Status { $v = Ly; [pscustomobject]@{ Status = $v -band 15; Lines = $v -shr 4 } } # 0 off, 1 looking, 2 found, 3 none, 4 offline
function Song([string]$t) { $a = [LyProbe]::GlobalAddAtom($t); $r = Ly 1 $a; [LyProbe]::GlobalDeleteAtom($a) | Out-Null; $r }
function Loads([int]$n) { Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge $n } 3000 | Out-Null; Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null }

& {
try {
    Set-Content -LiteralPath $ini -Value '[Music]', 'Lyrics=0' -Encoding ASCII
    Start-Notch
    Assert ((Song 'Song One|The Band|200') -eq 0 -and $sync.Requests.Count -eq 0 -and (Status).Status -eq 0) 'Lyrics off: nothing is looked up'

    Set-Content -LiteralPath $ini -Value '[Music]', 'Lyrics=1' -Encoding ASCII
    Loads 2
    Assert ((Song 'Song One|The Band|200') -eq 1) 'Lyrics on: the song is looked up'
    Assert (Wait-Until { (Status).Status -eq 2 } 5000) "lyrics found (status $((Status).Status))"
    Assert ($sync.Requests -contains '/api/get?track_name=Song%20One&artist_name=The%20Band&duration=200') 'asked by title, artist and length' # (a real player's song may be looked up too)
    Assert ((Status).Lines -eq 4) "4 timed lines: a line with two times counts twice, tags are skipped ($((Status).Lines))"
    $at = @{ 0 = -1; 999 = -1; 1000 = 0; 5499 = 0; 5500 = 1; 10000 = 2; 12000 = 3; 60000 = 3 }
    $bad = @(); foreach ($k in $at.Keys) { if ((Ly 2 $k) -ne $at[$k]) { $bad += "$k ms: $(Ly 2 $k) want $($at[$k])" } }
    Assert ($bad.Count -eq 0) "the line at each moment ($($bad -join '; '))"
    $before = $sync.Requests.Count
    Song 'Song One|The Band|200' | Out-Null
    Assert ($sync.Requests.Count -eq $before) 'the same song is not looked up again'

    Set-Content -LiteralPath $ini -Value '[Music]', 'Lyrics=1', 'Earlier=600' -Encoding ASCII
    Loads 3
    Song 'Song One|The Band|200' | Out-Null # again, in case a real player's song was looked up meanwhile
    Wait-Until { (Status).Status -eq 2 -and (Status).Lines -eq 4 } 5000 | Out-Null
    Assert ((Ly 2 4900) -eq 1 -and (Ly 2 4800) -eq 0) 'Earlier=600 shows each line 0.6 s early'

    Song 'Other|The Band|0' | Out-Null
    Assert (Wait-Until { (Status).Status -eq 2 } 5000) 'no exact match: found by search'
    Assert (($sync.Requests | Select-Object -Last 2)[0] -like '/api/get?track_name=Other*' -and ($sync.Requests | Select-Object -Last 1) -like '/api/search?track_name=Other*') '...after the exact lookup missed (a song length of 0 is left out)'
    Assert ((Status).Lines -eq 1) '...using the first result that has synced lyrics'

    Song 'Unknown|Nobody|100' | Out-Null
    Assert (Wait-Until { (Status).Status -eq 3 } 5000) 'a song with no synced lyrics says so'

    Set-Content -LiteralPath $ini -Value '[Music]', 'Lyrics=0' -Encoding ASCII
    Loads 4
    Assert ((Status).Status -eq 0 -and (Status).Lines -eq 0) 'turning Lyrics off forgets them'

    $p = Get-Process Perch
    Start-Sleep -Seconds 1
    $c0 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 4; $p.Refresh()
    Assert (($p.TotalProcessorTime.TotalMilliseconds - $c0) -lt 30) "idle CPU ~0 ($([int]($p.TotalProcessorTime.TotalMilliseconds - $c0)) ms over 4 s)"
} finally {
    Stop-Notch
    try { $listener.Stop() } catch {}
    $server.Dispose()
    Remove-Item Env:\PERCH_DATA_DIR, Env:\PERCH_LRCLIB_URL -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'lyrics'
