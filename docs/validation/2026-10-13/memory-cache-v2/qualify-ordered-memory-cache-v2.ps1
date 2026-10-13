$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\memory-cache-v2";$out="$r\vm\ordered-memory-cache-v2-expanded"
$q=Get-Content "$r\vm\memory-cache-v2-qualification\report.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.npu_qualified -or $q.cases.Count -ne 4){throw 'Runtime hardware qualification missing'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer active'}
$reference="$r\vm\ordered-point-reentry-v1-expanded\report.json"
$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$runtime="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"
& $py "$r\vm\replay-ordered-trace-v9-memory.py" --trace "$r\vm\indexed-dwm-stream-v120" --trace-manifest "$r\vm\v120-trace-sha256.json" --worker "$b\Release\nr_registered_indexed_worker.exe" --runtime $runtime --cache "$r\vm\model-cache-2026-4-v209" --output $out --seconds 1800 --limit 1438 --shared-queue 1 --pipeline-cache expanded --reference $reference --memory-cache
if($LASTEXITCODE){throw 'Full memory cache replay failed'}
$m=Get-Content "$out\report.json" -Raw|ConvertFrom-Json;$rows=@($m.requests|ForEach-Object {$_.coverage_telemetry})
if(!$m.passed -or !$m.completed -or $rows.Count -ne 1438){throw 'Incomplete memory cache replay'}
$hits=($rows|Measure-Object memory_cache_hits -Sum).Sum
[ordered]@{completed=$true;passed=$true;request_count=1438;exact_pixels_and_all_receipts=$true;memory_cache_hits=$hits;memory_reuse_observed=($hits -gt 0);backend_compile_calls=($rows|Measure-Object backend_compile_calls -Sum).Sum;memory_cache_evictions=($rows|Measure-Object memory_cache_evictions -Sum).Sum;compile_calls=($rows|Measure-Object compile_calls -Sum).Sum;disk_cache_hits=($rows|Measure-Object cache_hits -Sum).Sum;elapsed_ms=$m.elapsed_ms;draw_ms=($rows|Measure-Object draw_ms -Sum).Sum;compile_and_draw_ms=($rows|Measure-Object compile_and_draw_ms -Sum).Sum;live_desktop_qualified=$false;tahoma_performance_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$r\vm\ordered-memory-cache-v2-summary.json"
'Memory cache replay completed with exact receipts'
