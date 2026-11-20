# Ask Perch: Paste key (stored encrypted in ask.key, not in settings.ini), the list of models the key may
# use, the model picked in settings, a question typed into the notch and sent with Enter, the answer shown
# (Markdown made plain), follow-up questions carrying the conversation, errors (bad key, unknown model),
# Copy answer, Esc. A small fake Gemini server on 127.0.0.1 stands in for Google (PERCH_GEMINI_URL), so no
# real key or network is used. Changes the clipboard (puts your text back). Own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== Ask Perch =='
Add-Type -AssemblyName System.Windows.Forms
if (-not ('AskProbe' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class AskProbe {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool AllowSetForegroundWindow(int pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern ushort GlobalAddAtom(string s);
    [DllImport("kernel32.dll")] public static extern ushort GlobalDeleteAtom(ushort a);
    [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
    [DllImport("user32.dll")] public static extern bool CloseClipboard();
}
'@
}
$work = Join-Path $env:TEMP "perch-ask-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$ini = Join-Path $work 'settings.ini'
$env:PERCH_DATA_DIR = $work
$key = 'AIzaTestKey0123456789abcdefghijklmnopq'

# ---- a fake Gemini API on 127.0.0.1 ----
$sync = [hashtable]::Synchronized(@{ Key = $key; Requests = [System.Collections.ArrayList]::Synchronized((New-Object System.Collections.ArrayList)) })
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$server = [powershell]::Create()
$server.AddScript({
    param($l, $sync)
    while ($true) {
        try { $c = $l.AcceptTcpClient() } catch { break }
        try {
            $s = $c.GetStream(); $r = New-Object IO.StreamReader($s, [Text.Encoding]::UTF8)
            $line = $r.ReadLine(); $len = 0; $k = ''
            while (($h = $r.ReadLine()) -ne $null -and $h -ne '') {
                if ($h -match '^Content-Length:\s*(\d+)') { $len = [int]$Matches[1] }
                if ($h -match '^x-goog-api-key:\s*(.+)$') { $k = $Matches[1].Trim() }
            }
            $sb = New-Object Text.StringBuilder; $bytes = 0 # Content-Length counts UTF-8 bytes, not characters
            while ($bytes -lt $len) { $ch = $r.Read(); if ($ch -lt 0) { break }; [void]$sb.Append([char]$ch); $bytes += [Text.Encoding]::UTF8.GetByteCount([string][char]$ch) }
            $body = $sb.ToString()
            $path = ($line -split ' ')[1]
            $sync.Requests.Add([pscustomobject]@{ Line = $line; Key = $k; Body = $body }) | Out-Null
            $code = '200 OK'
            if ($k -ne $sync.Key) { $code = '400 Bad Request'; $out = '{"error":{"code":400,"message":"API key not valid. Please pass a valid API key.","status":"INVALID_ARGUMENT"}}' }
            elseif ($path.StartsWith('/v1beta/models?')) {
                $out = '{"models":[{"name":"models/gemini-flash-latest","displayName":"Gemini Flash Latest","supportedGenerationMethods":["generateContent","countTokens"]},' +
                       '{"name":"models/gemini-9.9-pro","supportedGenerationMethods":["generateContent"]},' +
                       '{"name":"models/text-embedding-004","supportedGenerationMethods":["embedContent"]},' +
                       '{"name":"models/gemini-9.9-flash","supportedGenerationMethods":["generateContent"]}]}'
            } elseif ($path -like '/v1beta/models/*:generateContent' -and $body -like '*Remind me to call Mum at 18:00*' -and $body -notlike '*Added*') {
                $out = '{"candidates":[{"content":{"role":"model","parts":[{"functionCall":{"name":"add_task","args":{"title":"Call Mum","start":"18:00","end":"18:30"}}}]}}]}'
            } elseif ($path -like '/v1beta/models/gemini-flash-latest:generateContent' -or $path -like '/v1beta/models/gemini-9.9-pro:generateContent') {
                $model = ($path -split '/')[3] -replace ':generateContent$', ''
                $out = '{"candidates":[{"content":{"role":"model","parts":[{"text":"**Paris** is the capital of France.\n* It sits on the Seine\n* Model ' + $model + '"}]}}]}'
            } else { $code = '404 Not Found'; $out = '{"error":{"code":404,"message":"models/x is not found","status":"NOT_FOUND"}}' }
            $b = [Text.Encoding]::UTF8.GetBytes($out)
            $hd = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $code`r`nContent-Type: application/json`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n")
            $s.Write($hd, 0, $hd.Length); $s.Write($b, 0, $b.Length); $s.Flush()
        } catch {} finally { $c.Close() }
    }
}).AddArgument($listener).AddArgument($sync) | Out-Null
$serverRun = $server.BeginInvoke()
$env:PERCH_GEMINI_URL = "http://127.0.0.1:$port/v1beta"

function Ask([int]$wp = 0) { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 36, [IntPtr]$wp, [IntPtr]::Zero) }
function St { $v = Ask; [pscustomobject]@{ Open = ($v -band 1) -ne 0; Busy = ($v -band 2) -ne 0; Key = ($v -band 4) -ne 0; Error = ($v -band 8) -ne 0; Len = $v -shr 8 } }
function Type-Text([string]$t) { foreach ($c in $t.ToCharArray()) { [AskProbe]::PostMessage((Get-NotchWindow), 0x102, [IntPtr][int]$c, [IntPtr]0) | Out-Null }; Start-Sleep -Milliseconds 150 }
function Key([int]$vk) { [AskProbe]::PostMessage((Get-NotchWindow), 0x100, [IntPtr]$vk, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 150 }
function Set-Clip([string]$t) { for ($i = 0; $i -lt 25; $i++) { try { [System.Windows.Forms.Clipboard]::SetDataObject($t, $true, 10, 50); Start-Sleep -Milliseconds 150; return } catch { Start-Sleep -Milliseconds 300 } }; throw 'another app kept the clipboard busy' }
function Last { $sync.Requests[$sync.Requests.Count - 1] }

$saved = $null
try { if ([System.Windows.Forms.Clipboard]::ContainsText()) { $saved = [System.Windows.Forms.Clipboard]::GetText() } } catch {}
& {
try {
    Set-Content -LiteralPath $ini -Value '[Ask]', 'Model=gemini-flash-latest' -Encoding ASCII
    Start-Notch
    Assert (-not (St).Key) 'no key to start with'
    Ask 1 | Out-Null
    Assert ((St).Open) 'Ask opens (as Ctrl + Alt + Space does)'
    Type-Text 'hi'; Key 0x0D
    Assert ((St).Error -and $sync.Requests.Count -eq 0) 'without a key nothing is sent; it says to add one'
    Key 0x1B
    Assert (-not (St).Open) 'Esc closes it'

    # --- Paste key (needs the clipboard; another app may be holding it) ---
    $clipFree = [AskProbe]::OpenClipboard([IntPtr]::Zero); if ($clipFree) { [AskProbe]::CloseClipboard() | Out-Null }
    if ($clipFree) {
        Set-Clip 'not a key'
        [NotchTest]::Command((Get-NotchWindow), 119)
        Assert (-not (St).Key) 'Paste key refuses text that is not a key'
        Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
        Set-Clip "  $key  "
        [NotchTest]::Command((Get-NotchWindow), 119)
        Assert (Wait-Until { (St).Key } 2000) 'Paste key saves the key'
    } else {
        Write-Host '  SKIP  Paste key: another app is holding the clipboard; the key goes in by a test message' -ForegroundColor Yellow
        $atom = [AskProbe]::GlobalAddAtom($key)
        [NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 36, [IntPtr]4, [IntPtr][int]$atom) | Out-Null
        [AskProbe]::GlobalDeleteAtom($atom) | Out-Null
        Assert ((St).Key) 'the key is saved'
    }
    $file = Join-Path $work 'ask.key'
    $raw = [IO.File]::ReadAllBytes($file)
    $plain = [Text.Encoding]::Unicode.GetString($raw) + [Text.Encoding]::ASCII.GetString($raw)
    Assert (-not $plain.Contains('AIzaTest') -and -not ((Get-Content $ini -Raw) -match 'AIza')) '...encrypted in ask.key, and not in settings.ini'
    Assert (Wait-Until { (Ask 2) -eq 3 } 4000) "the models the key may use are listed (text models only: $(Ask 2))"
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null

    # --- a question ---
    [AskProbe]::AllowSetForegroundWindow((Get-Process Perch).Id) | Out-Null # as the hotkey would
    Ask 1 | Out-Null
    Key 0x08; Key 0x08 # the "hi" typed before the key was added is still there
    Type-Text 'What is the capital of France?'; Key 0x0D
    Assert (Wait-Until { -not (St).Busy -and (St).Len -gt 0 } 6000) "an answer comes back ($((St).Len) chars)"
    $q = Last
    Assert ($q.Line -like 'POST /v1beta/models/gemini-flash-latest:generateContent*' -and $q.Key -eq $key) 'sent to the chosen model, with the key in a header (not the address)'
    Assert ($q.Body -like '*"role":"user"*capital of France*') 'the question is in the request'
    Assert ((St).Len -eq 'Paris is the capital of France.'.Length + 1 + '. It sits on the Seine'.Length + 1 + '. Model gemini-flash-latest'.Length) 'Markdown is made plain (no **, bullets as dots)'
    Write-Host "  screenshot: $(Save-TopShot 'ask-answer')"
    Type-Text 'And Germany?'
    Key 0x0D
    Assert (Wait-Until { -not (St).Busy -and (St).Len -gt 0 } 6000) 'a follow-up question'
    Assert ((Last).Body -like '*capital of France*"role":"model"*Paris*And Germany?*') '...carries the conversation so far'

    # --- Copy answer (a click on the open notch) ---
    if ($clipFree -and (Wait-Until { Test-Flag Expanded } 1500)) {
        $v = [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 20, [IntPtr]275, [IntPtr]::Zero)
        [AskProbe]::PostMessage((Get-NotchWindow), 0x202, [IntPtr]0, [IntPtr]$v) | Out-Null; Start-Sleep -Milliseconds 300
        Assert ([System.Windows.Forms.Clipboard]::GetText() -like 'Paris is the capital of France.*') 'Copy answer puts it on the clipboard'
    } else { Write-Host '  SKIP  Copy answer: the clipboard is busy, or the notch could not take the keyboard from this test' -ForegroundColor Yellow }
    Key 0x1B

    # --- the one thing it can do: add a to-do task, for real ---
    Ask 1 | Out-Null; Type-Text 'Remind me to call Mum at 18:00'; Key 0x0D
    Assert (Wait-Until { -not (St).Busy -and (St).Len -gt 0 } 6000) 'asked to add a task'
    Assert ((Last).Body -like '*"functionDeclarations"*"add_task"*' -and (Last).Body -like '*only action you can take*') 'the request offers one tool, add_task, and says it is the only action'
    $tasks = Join-Path $work 'tasks.txt'
    Assert ((Test-Path $tasks) -and ((Get-Content $tasks -Encoding UTF8) -match "`t1080`t1110`t0`tCall Mum$")) "the task is really added, 18:00-18:30 ($(if (Test-Path $tasks) { Get-Content $tasks -Encoding UTF8 }))"
    Key 0x1B

    # --- the model is the user's choice ---
    Set-Content -LiteralPath $ini -Value '[Ask]', 'Model=gemini-9.9-pro' -Encoding ASCII
    Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge 2 } 3000 | Out-Null
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Ask 1 | Out-Null; Type-Text 'Hello'; Key 0x0D
    Assert (Wait-Until { -not (St).Busy -and (St).Len -gt 0 } 6000) 'another model answers'
    Assert ((Last).Line -like 'POST /v1beta/models/gemini-9.9-pro:generateContent*') 'the question goes to the model picked in settings'
    Key 0x1B
    Set-Content -LiteralPath $ini -Value '[Ask]', 'Model=gemini-0-gone' -Encoding ASCII
    Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge 3 } 3000 | Out-Null
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Ask 1 | Out-Null; Type-Text 'Hello'; Key 0x0D
    Assert (Wait-Until { -not (St).Busy -and (St).Error } 6000) 'a model the key cannot use shows an error'
    Key 0x1B

    # --- a key Google refuses ---
    $sync.Key = 'something-else'
    Set-Content -LiteralPath $ini -Value '[Ask]', 'Model=gemini-flash-latest' -Encoding ASCII
    Wait-Until { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 15, [IntPtr]6, [IntPtr]::Zero) -ge 4 } 3000 | Out-Null
    Wait-Until { -not (Test-Flag Toast) } 7000 | Out-Null
    Ask 1 | Out-Null; Type-Text 'Hello'; Key 0x0D
    Assert (Wait-Until { -not (St).Busy -and (St).Error } 6000) 'a key Google refuses shows an error'
    Key 0x1B

    $p = Get-Process Perch
    Start-Sleep -Seconds 1
    $c0 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep -Seconds 4; $p.Refresh()
    if (Test-Flag Playing) { Write-Host '  SKIP  idle CPU: media is playing (the sound bars move)' -ForegroundColor Yellow }
    else { Assert (($p.TotalProcessorTime.TotalMilliseconds - $c0) -lt 30) "idle CPU ~0 ($([int]($p.TotalProcessorTime.TotalMilliseconds - $c0)) ms over 4 s)" }
    Assert ($p.WorkingSet64 / 1MB -lt 13) ("working set {0:N1} MB < 13 MB" -f ($p.WorkingSet64 / 1MB))
} finally {
    Stop-Notch
    try { $listener.Stop() } catch {}
    $server.Dispose()
    try { if ($null -ne $saved) { [System.Windows.Forms.Clipboard]::SetText($saved) } else { [System.Windows.Forms.Clipboard]::Clear() } } catch {}
    Remove-Item Env:\PERCH_DATA_DIR, Env:\PERCH_GEMINI_URL -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'ask'
