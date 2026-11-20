# perchctl (rust\cli): the command line adds, lists, ticks and removes tasks and logs water in the same files
# as the app, a running Perch picks each change up at once, bad input is refused with a clear message, and
# `perchctl mcp` answers an AI tool over MCP (initialize, tools/list, tools/call). Builds perchctl with cargo
# if needed. Uses its own temporary data folder.
. "$PSScriptRoot\lib.ps1"
Write-Host '== perchctl (CLI and MCP) =='
$cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
$ctl = Join-Path $Root 'rust\target\release\perchctl.exe'
if (Test-Path $cargo) {
    Push-Location (Join-Path $Root 'rust'); & $cargo build --release --quiet 2>&1 | Out-Null; Pop-Location
}
if (-not (Test-Path $ctl)) { Write-Host '  SKIP  perchctl: Rust is not installed (rustup.rs)' -ForegroundColor Yellow; Finish-Test 'cli'; return }
$work = Join-Path $env:TEMP "perch-cli-test-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$env:PERCH_DATA_DIR = $work
function Ctl { $ErrorActionPreference = 'Continue'; $out = & $ctl @args 2>&1 | ForEach-Object { "$_" } | Out-String; [pscustomobject]@{ Code = $LASTEXITCODE; Out = $out.Trim() } } # error text arrives on stderr
function Todo { $v = [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 27, [IntPtr]::Zero, [IntPtr]::Zero); [pscustomobject]@{ Tasks = $v -band 255; Done = ($v -shr 8) -band 255 } }
function Water { [int64][NotchTest]::SendMessage((Get-NotchWindow), [NotchTest]::WM_APP + 33, [IntPtr]::Zero, [IntPtr]::Zero) }

& {
try {
    # --- without the app: the files are enough ---
    $r = Ctl task add 'Water the plants' --daily
    Assert ($r.Code -eq 0 -and $r.Out -eq 'Added task 1: Water the plants, every day') "add a task with Perch closed ($($r.Out))"
    Start-Notch
    Assert ((Todo).Tasks -eq 1) 'Perch starts with it'

    # --- with the app running: it follows each change at once ---
    $r = Ctl task add 'Call Mum' --at 18:00 --until 18:30
    Assert ($r.Code -eq 0 -and $r.Out -eq 'Added task 2: Call Mum (18:00-18:30)') "add a task with times ($($r.Out))"
    Assert ((Todo).Tasks -eq 2) 'the running Perch shows it straight away'
    $j = (Ctl task list --json).Out | ConvertFrom-Json
    Assert ($j.tasks.Count -eq 2 -and $j.tasks[1].title -eq 'Call Mum' -and $j.tasks[1].start -eq '18:00' -and $j.tasks[1].end -eq '18:30' -and $j.tasks[0].daily) 'task list --json'
    $r = Ctl task list
    $lines = @($r.Out -split "`r?`n")
    Assert ($lines[0] -eq '0 of 2 done today:' -and $lines[1] -eq '1. [ ] Water the plants  (daily)' -and $lines[2] -eq '2. [ ] Call Mum  18:00-18:30') "task list ($($lines -join ' / '))"
    Assert ((Ctl task done 2).Code -eq 0 -and (Todo).Done -eq 1) 'task done 2 ticks it in Perch too'
    Assert ((Ctl task done 2 --undo).Code -eq 0 -and (Todo).Done -eq 0) '...and --undo un-ticks it'

    # --- what isn't accepted ---
    $r = Ctl task add 'Late' --until 10:00
    Assert ($r.Code -eq 1 -and $r.Out -like '*an end time needs a start time*') "an end without a start is refused ($($r.Out))"
    $r = Ctl task add 'Odd' --at 25:00
    Assert ($r.Code -eq 1 -and $r.Out -like '*isn''t a time like 18:30*') "a bad time is refused ($($r.Out))"
    $r = Ctl task remove 9
    Assert ($r.Code -eq 1 -and $r.Out -like '*there is no task 9; there are 2*') "a task that isn't there ($($r.Out))"
    Assert ((Todo).Tasks -eq 2) '...and nothing changed'

    # --- water ---
    $r = Ctl water add
    Assert ($r.Code -eq 0 -and $r.Out -eq 'Added 250 ml: 250 ml of 2 L today' -and (Water) -eq 250) "water add: one cup ($($r.Out); Perch shows $(Water))"
    $r = Ctl water add 1000
    Assert ((Water) -eq 1250 -and $r.Out -like '*1.25 L of 2 L today') "water add 1000 ($($r.Out))"
    $r = Ctl water add -250
    Assert ((Water) -eq 1000 -and $r.Out -like 'Took back 250 ml*') "a negative amount takes some back ($($r.Out))"
    $j = (Ctl water status --json).Out | ConvertFrom-Json
    Assert ($j.ml -eq 1000 -and $j.goal_ml -eq 2000 -and -not $j.goal_reached) 'water status --json'

    Assert ((Ctl task remove 1).Code -eq 0 -and (Todo).Tasks -eq 1) 'task remove 1'

    # --- MCP ---
    $psi = New-Object System.Diagnostics.ProcessStartInfo $ctl, 'mcp'
    $psi.UseShellExecute = $false; $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables['PERCH_DATA_DIR'] = $work
    $mcp = [System.Diagnostics.Process]::Start($psi)
    function Rpc([string]$json) { $mcp.StandardInput.WriteLine($json); $mcp.StandardInput.Flush(); $mcp.StandardOutput.ReadLine() | ConvertFrom-Json }
    $r = Rpc '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}'
    Assert ($r.id -eq 1 -and $r.result.protocolVersion -eq '2025-06-18' -and $r.result.serverInfo.name -eq 'perch' -and $r.result.capabilities.tools) 'MCP initialize'
    $mcp.StandardInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}')
    $r = Rpc '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
    $names = @($r.result.tools | ForEach-Object { $_.name })
    Assert ($r.id -eq 2 -and ($names -join ',') -eq 'add_task,list_tasks,complete_task,remove_task,add_water,water_status') "MCP tools/list ($($names -join ', '))"
    $r = Rpc '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add_task","arguments":{"title":"Stretch","start":"16:00"}}}'
    Assert (-not $r.result.isError -and $r.result.content[0].text -eq 'Added task 2: Stretch (16:00)' -and (Todo).Tasks -eq 2) "MCP add_task, and Perch shows it ($($r.result.content[0].text))"
    $r = Rpc '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"add_water","arguments":{"ml":500}}}'
    Assert ($r.result.structuredContent.ml -eq 1500 -and (Water) -eq 1500) 'MCP add_water'
    $r = Rpc '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"complete_task","arguments":{"number":7}}}'
    Assert ($r.result.isError -and $r.result.content[0].text -like '*no task 7*') 'MCP: a tool error comes back as a result with isError'
    $r = Rpc '{"jsonrpc":"2.0","id":6,"method":"resources/list"}'
    Assert ($r.error.code -eq -32601) 'MCP: an unknown method gets a JSON-RPC error'
    $mcp.StandardInput.Close()
    Assert ($mcp.WaitForExit(3000)) 'MCP server ends when its input closes'
} finally {
    Stop-Notch
    Remove-Item Env:\PERCH_DATA_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
}
Finish-Test 'cli'
