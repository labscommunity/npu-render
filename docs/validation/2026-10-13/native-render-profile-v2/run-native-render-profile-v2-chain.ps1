$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-render-profile-v2';$report="$r\vm\$name-chain.json";$started=[DateTime]::UtcNow
if(Test-Path $report){throw 'Chain evidence exists'}
$state=[ordered]@{started=$started.ToString('o');controller_pid=$PID;completed=$false;passed=$false;phase='waiting-for-clean-build';cases=@()}
function Save-State {$state|ConvertTo-Json -Depth 6|Set-Content $report}
Save-State
try {
 $deadline=$started.AddMinutes(15)
 while(!(Test-Path "$r\builds\$name\registered-build-report.json")){if([DateTime]::UtcNow -gt $deadline){throw 'Qualified build did not arrive'};Start-Sleep -Milliseconds 500}
 $state.phase='npu-components';Save-State
 & ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\qualify-$name.ps1")))
 $qualified=Get-Content "$r\vm\$name-qualification\report.json" -Raw|ConvertFrom-Json
 if(!$qualified.completed -or !$qualified.npu_qualified -or $qualified.cases.Count -ne 5){throw 'NPU components incomplete'}
 foreach($case in @(@{name='combined';memory=$true;compact=$true})){
  if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_worker,nr_registered_indexed_stream_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer active'}
  $state.phase=$case.name;Save-State;$out="$r\vm\$name-$($case.name)"
  $arguments=@("$r\vm\replay-ordered-trace-v12-profile.py",'--trace',"$r\vm\indexed-dwm-stream-v120",'--trace-manifest',"$r\vm\v120-trace-sha256.json",'--worker',"$r\builds\$name\Release\nr_registered_local_indexed_worker.exe",'--runtime',"$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs",'--cache',"$r\vm\model-cache-2026-4-v209",'--output',$out,'--seconds','1800','--limit','1438','--shared-queue','1','--pipeline-cache','expanded','--reference',"$r\vm\ordered-point-reentry-v1-expanded\report.json")
  if($case.memory){$arguments+='--memory-cache'};if($case.compact){$arguments+='--compact-coverage'}
  & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" @arguments
  if($LASTEXITCODE){throw 'Profile replay failed'}
  $m=Get-Content "$out\report.json" -Raw|ConvertFrom-Json;$rows=@($m.requests|ForEach-Object {$_.coverage_telemetry});$logs=@(Get-Content "$out\worker.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$acks=@($logs|Where-Object {$_.worker_success});$done=@($logs|Where-Object {$_.local_worker_finished})
  if(!$m.passed -or !$m.completed -or $rows.Count -ne 1438 -or $acks.Count -ne 1438 -or $done.Count -ne 1 -or $done[0].succeeded -ne 1438 -or $done[0].settled -ne 1438){throw 'Incomplete delivery evidence'}
  if($case.memory -and ($rows|Measure-Object memory_cache_hits -Sum).Sum -le 0){throw 'Cache optimization was not exercised'}
  if($case.compact -and ($rows|Measure-Object compact_coverage_calls -Sum).Sum -le 0){throw 'Compact coverage was not exercised'}
  $state.cases+=@([ordered]@{name=$case.name;memory_cache_enabled=$case.memory;compact_coverage_enabled=$case.compact;passed=$true;requests=1438;acks=$acks.Count;worker_sha256=$m.worker_sha256;elapsed_ms=$m.elapsed_ms;compile_and_draw_ms=($rows|Measure-Object compile_and_draw_ms -Sum).Sum;draw_ms=($rows|Measure-Object draw_ms -Sum).Sum;memory_hits=($rows|Measure-Object memory_cache_hits -Sum).Sum;backend_compiles=($rows|Measure-Object backend_compile_calls -Sum).Sum;compact_calls=($rows|Measure-Object compact_coverage_calls -Sum).Sum;compact_samples=($rows|Measure-Object compact_coverage_samples -Sum).Sum;wide_calls=($rows|Measure-Object wide_coverage_calls -Sum).Sum;all_pixels_exact=$true;all_receipts_exact_or_exact_compact_input_delta=$true});Save-State
 }

 $state.passed=$true;$state.phase='complete'
} catch {$state.error=$_.Exception.Message;$state.phase='failed';throw}
finally {$state.completed=$true;$state.finished=[DateTime]::UtcNow.ToString('o');Save-State}
'Corrected combined profile qualification completed'
