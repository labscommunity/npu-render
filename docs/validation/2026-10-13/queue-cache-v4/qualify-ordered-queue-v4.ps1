# Replay the complete successful desktop trace without VM or network rendering transport.
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\queue-worker-v4"
$prior=Get-Content "$r\vm\indexed-dwm-stream-v120\report.json" -Raw|ConvertFrom-Json
if(!$prior.completed -or !$prior.configuration_removed -or (Get-Process -Id $prior.worker_pid -ErrorAction SilentlyContinue)){throw 'Desktop trial must finish before replay'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another NPU renderer is running'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.per_request_compile_telemetry){throw 'Build qualification missing'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
$smoke=Get-Content "$r\vm\queue-worker-v4-smoke-qualified.json" -Raw|ConvertFrom-Json
if(!$smoke.all_checks_passed -or !$smoke.default_shared_expanded_exact){throw 'Worker smoke required'}
$trace=Get-Content "$r\vm\v120-trace-sha256.json" -Raw|ConvertFrom-Json
if($trace.Count -ne 1438 -or ($trace|Measure-Object bytes -Sum).Sum -ne 4398396820){throw 'Full v120 trace required'}
$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$runtime="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"
$cases=@()
foreach($variant in 'baseline','shared','expanded'){
 $nrReplayArgs=@("$r\vm\replay-ordered-trace-v3.py",'--trace',"$r\vm\indexed-dwm-stream-v120",'--trace-manifest',"$r\vm\v120-trace-sha256.json",'--worker',"$b\Release\nr_registered_indexed_worker.exe",'--runtime',$runtime,'--cache',"$r\vm\model-cache-2026-4-v209",'--output',"$r\vm\ordered-queue-v4-$variant",'--seconds','1800','--limit','1438')
 if($variant -ne 'baseline'){$nrReplayArgs+=@('--shared-queue','1','--reference',"$r\vm\ordered-queue-v4-baseline\report.json")}
 if($variant -eq 'expanded'){$nrReplayArgs+=@('--pipeline-cache','expanded')}
 & $py @nrReplayArgs
 if($LASTEXITCODE){throw "Ordered trace failed for $variant; no subsequent variant will run"}
 $report=Get-Content "$r\vm\ordered-queue-v4-$variant\report.json" -Raw|ConvertFrom-Json
 $rows=@(Get-Content "$r\vm\ordered-queue-v4-$variant\worker.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json})
 $success=@($rows|Where-Object {$_.passed});$finish=@($rows|Where-Object {$_.worker_finished});$policy=@($rows|Where-Object {$_.kind -eq 'pipeline_cache_policy'})
 if(!$report.passed -or $success.Count -ne 1438 -or $finish.Count -ne 1 -or $finish[0].succeeded -ne 1438 -or $finish[0].failed -ne 0){throw 'Incomplete replay'}
 if(@($success|Where-Object {$_.execution_device -ne 'NPU' -or $null -eq $_.compile_and_draw_ms -or $null -eq $_.compile_calls}).Count){throw 'Missing NPU timing telemetry'}
 $uniform=if($variant -eq 'expanded'){16}else{8};$triangle=if($variant -eq 'expanded'){48}else{24}
 if($policy.Count -ne 1 -or $policy[0].uniform_capacity -ne $uniform -or $policy[0].triangle_capacity -ne $triangle -or $policy[0].clear_capacity -ne 4){throw 'Cache capacity readback mismatch'}
 $queue=@($rows|Where-Object {$_.kind -eq 'private_shared_queue_experiment'})
 if($variant -eq 'baseline'){if($queue.Count){throw 'Unexpected baseline override'}}elseif($queue.Count -ne 1 -or !$queue[0].enabled -or !$queue[0].property_readback_verified){throw 'Private queue readback mismatch'}
 $cases+=[ordered]@{variant=$variant;elapsed_ms=$report.elapsed_ms;draw_ms=($success|Measure-Object draw_ms -Sum).Sum;compile_and_draw_ms=($success|Measure-Object compile_and_draw_ms -Sum).Sum;compile_calls=($success|Measure-Object compile_calls -Sum).Sum;disk_cache_hits=($success|Measure-Object cache_hits -Sum).Sum;passed=$true}
 [ordered]@{completed=$false;cases=$cases;desktop_use_qualified=$false;hardware_queue_count_measured=$false}|ConvertTo-Json -Depth 5|Set-Content "$r\vm\ordered-queue-v4-summary.json"
}
[ordered]@{completed=$true;all_three_exact=$true;request_count_per_variant=1438;cases=$cases;first_pass_order='baseline, shared, expanded';warming_and_order_effects_controlled=$false;desktop_use_qualified=$false;hardware_queue_count_measured=$false;tahoma_performance_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$r\vm\ordered-queue-v4-summary.json"
'Full ordered trace passed all three configurations. First-pass timings require warming and order review before attributing a speedup.'
