$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\compact-runtime-v1-replay-memory.jsonl";$path="$r\builds\compact-runtime-v1\Release\nr_registered_indexed_worker.exe"
if(Test-Path $out){throw 'Memory evidence exists'};$until=[DateTime]::UtcNow.AddMinutes(33);$owned=$null
while([DateTime]::UtcNow -lt $until -and !$owned){$matches=@(Get-CimInstance Win32_Process -Filter "Name='nr_registered_indexed_worker.exe'"|Where-Object {$_.ExecutablePath -eq $path});if($matches.Count -gt 1){throw 'Ambiguous worker'};if($matches.Count){$owned=Get-Process -Id $matches[0].ProcessId}else{Start-Sleep -Seconds 2}}
if(!$owned){throw 'Worker never started'}
try{while([DateTime]::UtcNow -lt $until){$owned.Refresh();if($owned.HasExited){break};[ordered]@{at=[DateTime]::UtcNow.ToString('o');pid=$owned.Id;working_bytes=$owned.WorkingSet64;private_bytes=$owned.PrivateMemorySize64;cpu_seconds=$owned.TotalProcessorTime.TotalSeconds}|ConvertTo-Json -Compress|Add-Content $out;Start-Sleep -Seconds 2}}finally{$owned.Dispose()}
'Finite memory watcher finished'
