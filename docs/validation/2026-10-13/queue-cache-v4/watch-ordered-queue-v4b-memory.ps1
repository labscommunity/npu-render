$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\ordered-queue-v4b-memory.jsonl"
if(Test-Path $out){throw 'Memory evidence exists'}
$watch=[Diagnostics.Stopwatch]::StartNew();$idle=0
while($watch.Elapsed.TotalSeconds -lt 5600){
 $processes=@(Get-Process nr_registered_indexed_worker -ErrorAction SilentlyContinue)
 foreach($p in $processes){[ordered]@{at=[DateTime]::UtcNow.ToString('o');pid=$p.Id;private_bytes=$p.PrivateMemorySize64;working_bytes=$p.WorkingSet64;peak_working_bytes=$p.PeakWorkingSet64;cpu_ms=$p.TotalProcessorTime.TotalMilliseconds}|ConvertTo-Json -Compress|Add-Content -Encoding UTF8 $out}
 if($processes.Count){$idle=0}else{$idle++}
 $summary="$r\vm\ordered-queue-v4b-summary.json"
 if(Test-Path $summary){$s=Get-Content $summary -Raw|ConvertFrom-Json;if($s.completed){break}}
 if($idle -ge 12){break}
 Start-Sleep -Seconds 5
}
'Finite replay memory observation finished'
