$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\input-reuse-v1";$out="$r\vm\ordered-input-reuse-v1-expanded"
$q=Get-Content "$r\vm\input-reuse-v1-qualification-retry\report.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.passed -or $q.cases.Count -ne 4){throw 'Hardware input/texture/loop qualification required'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.immutable_per_draw_inputs){throw 'Build qualification missing'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
$reference="$r\vm\ordered-queue-v4c-expanded\report.json";$baseline=Get-Content $reference -Raw|ConvertFrom-Json
if(!$baseline.completed -or !$baseline.passed -or $baseline.saved_requests -ne 1438){throw 'Completed reference required'}
$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$runtime="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"
& $py "$r\vm\replay-ordered-trace-v5.py" --trace "$r\vm\indexed-dwm-stream-v120" --trace-manifest "$r\vm\v120-trace-sha256.json" --worker "$b\Release\nr_registered_indexed_worker.exe" --runtime $runtime --cache "$r\vm\model-cache-2026-4-v209" --output $out --seconds 1800 --limit 1438 --shared-queue 1 --pipeline-cache expanded --reference $reference
if($LASTEXITCODE){throw 'Full input-reuse replay failed'}
$report=Get-Content "$out\report.json" -Raw|ConvertFrom-Json
$rows=@(Get-Content "$out\worker.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json})
$success=@($rows|Where-Object {$_.passed});$finish=@($rows|Where-Object {$_.worker_finished});$policy=@($rows|Where-Object {$_.kind -eq 'pipeline_cache_policy'});$queue=@($rows|Where-Object {$_.kind -eq 'private_shared_queue_experiment'})
if(!$report.passed -or $success.Count -ne 1438 -or $finish.Count -ne 1 -or $finish[0].succeeded -ne 1438 -or $finish[0].failed -ne 0){throw 'Incomplete replay'}
if(@($success|Where-Object {$_.execution_device -ne 'NPU' -or $null -eq $_.compile_and_draw_ms -or $null -eq $_.compile_calls -or $null -eq $_.shader_ms}).Count){throw 'Missing timing telemetry'}
if($policy.Count -ne 1 -or $policy[0].uniform_capacity -ne 16 -or $policy[0].triangle_capacity -ne 48 -or $policy[0].clear_capacity -ne 4 -or $queue.Count -ne 1 -or !$queue[0].enabled -or !$queue[0].property_readback_verified){throw 'Configuration readback failed'}
[ordered]@{completed=$true;passed=$true;build='input-reuse-v1';request_count=1438;all_pixels_and_receipts_exact=$true;reference='ordered-queue-v4c-expanded';elapsed_ms=$report.elapsed_ms;reference_elapsed_ms=$baseline.elapsed_ms;draw_ms=($success|Measure-Object draw_ms -Sum).Sum;shader_ms=($success|Measure-Object shader_ms -Sum).Sum;projection_ms=($success|Measure-Object projection_ms -Sum).Sum;coverage_ms=($success|Measure-Object coverage_ms -Sum).Sum;merger_ms=($success|Measure-Object merger_ms -Sum).Sum;compile_and_draw_ms=($success|Measure-Object compile_and_draw_ms -Sum).Sum;compile_calls=($success|Measure-Object compile_calls -Sum).Sum;disk_cache_hits=($success|Measure-Object cache_hits -Sum).Sum;warming_and_order_effects_controlled=$false;desktop_use_qualified=$false;physical_dma_savings_measured=$false;tahoma_performance_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$r\vm\ordered-input-reuse-v1-summary.json"
'Full input-reuse trace passed all 1438 pixel and receipt comparisons'
