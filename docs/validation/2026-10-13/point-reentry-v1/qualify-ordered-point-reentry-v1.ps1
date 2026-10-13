$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\point-reentry-v1";$out="$r\vm\ordered-point-reentry-v1-expanded"
$q=Get-Content "$r\vm\point-reentry-v1-NPU-qualified.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.npu_qualified -or !$q.independent_scalar_oracle -or !$q.baseline_gpu_failure_reproduced){throw 'Scoped hardware crop transition qualification required'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.point_crop_reentry){throw 'Build qualification missing'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
$reference="$r\vm\ordered-queue-v4c-expanded\report.json";$baseline=Get-Content $reference -Raw|ConvertFrom-Json
if(!$baseline.completed -or !$baseline.passed -or $baseline.saved_requests -ne 1438){throw 'Completed reference required'}
$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$runtime="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"
& $py "$r\vm\replay-ordered-trace-v6-point.py" --trace "$r\vm\indexed-dwm-stream-v120" --trace-manifest "$r\vm\v120-trace-sha256.json" --worker "$b\Release\nr_registered_indexed_worker.exe" --runtime $runtime --cache "$r\vm\model-cache-2026-4-v209" --output $out --seconds 1800 --limit 1438 --shared-queue 1 --pipeline-cache expanded --reference $reference --allow-point-reentry
if($LASTEXITCODE){throw 'Full crop-reentry replay failed'}
$report=Get-Content "$out\report.json" -Raw|ConvertFrom-Json;$rows=@(Get-Content "$out\worker.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$success=@($rows|Where-Object {$_.passed});$finish=@($rows|Where-Object {$_.worker_finished})
if(!$report.passed -or $success.Count -ne 1438 -or $finish.Count -ne 1 -or $finish[0].succeeded -ne 1438 -or $finish[0].failed -ne 0 -or @($success|Where-Object {$_.execution_device -ne 'NPU'}).Count){throw 'Incomplete NPU replay'}
$changed=@($report.requests|Where-Object {!$_.reference_output_and_receipt_exact});$readmitted=@($changed|Where-Object {$_.reference_comparison -eq 'point_crop_readmitted'})
if($readmitted.Count -lt 3){throw 'Expected later crops were not readmitted'}
foreach($ordinal in @(1326,1370,1432)){if($report.requests[$ordinal-1].reference_comparison -ne 'point_crop_readmitted'){throw 'Known stale crop was not fixed'}}
[ordered]@{completed=$true;passed=$true;build='point-reentry-v1';request_count=1438;all_pixels_and_coverage_exact=$true;non_point_receipts_exact=$true;changed_receipts=$changed;crop_readmitted_count=$readmitted.Count;reference='ordered-queue-v4c-expanded';elapsed_ms=$report.elapsed_ms;reference_elapsed_ms=$baseline.elapsed_ms;draw_ms=($success|Measure-Object draw_ms -Sum).Sum;compile_and_draw_ms=($success|Measure-Object compile_and_draw_ms -Sum).Sum;compile_calls=($success|Measure-Object compile_calls -Sum).Sum;disk_cache_hits=($success|Measure-Object cache_hits -Sum).Sum;provider_requests=($success|Measure-Object provider_requests -Sum).Sum;warming_and_order_effects_controlled=$false;desktop_use_qualified=$false;tahoma_performance_tested=$false}|ConvertTo-Json -Depth 8|Set-Content "$r\vm\ordered-point-reentry-v1-summary.json"
'Full crop-reentry trace passed all 1438 pixel and coverage comparisons'
