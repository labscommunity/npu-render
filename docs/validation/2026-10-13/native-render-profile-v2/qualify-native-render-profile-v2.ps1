$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-render-profile-v2";$out="$r\vm\native-render-profile-v2-qualification"
if(Test-Path $out){throw 'Evidence exists'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.compiled_memory_cache_opt_in){throw 'Runtime qualification absent'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Renderer active'}
New-Item -ItemType Directory $out|Out-Null;$old=$env:PATH;$oldCompact=$env:NR_RENDER_MEMORY_CACHE;$oldCache=$env:NR_RENDER_NPU_CACHE;$oldQueue=$env:NR_RENDER_SHARED_QUEUE;$records=[Collections.ArrayList]::new()
try {
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$old";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_MEMORY_CACHE='1';$env:NR_RENDER_COMPACT_COVERAGE='1';$env:NR_RENDER_SHARED_QUEUE='1'
 foreach($test in 'provider_memory_cache','compact_coverage','raster','uniform_draw','triangle_texture_draw'){
  $exe="$b\Release\nr_registered_${test}_test.exe";if(!(Test-Path $exe)){throw 'Test executable missing'}
  $watch=[Diagnostics.Stopwatch]::StartNew();$p=Start-Process $exe -ArgumentList @('NPU') -RedirectStandardOutput "$out\NPU-$test.stdout.jsonl" -RedirectStandardError "$out\NPU-$test.stderr.log" -PassThru;[void]$p.Handle
  try{if(!$p.WaitForExit(900000)){$p.Kill();throw 'Runtime hardware test timed out'};$p.Refresh();if($p.ExitCode){throw "Runtime hardware test failed: $test"}}finally{$p.Dispose()}
  $rows=@(Get-Content "$out\NPU-$test.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$deviceRows=@($rows|Where-Object {$_.execution_device})
  if(!$deviceRows.Count -or @($deviceRows|Where-Object {$_.execution_device -ne 'NPU'}).Count){throw 'Missing or wrong device receipts'}
  if($test -eq 'provider_memory_cache' -and ($deviceRows.Count -ne 1 -or !$deviceRows[0].passed -or $deviceRows[0].verified_values -ne 2368 -or $deviceRows[0].memory_hits -ne 2 -or $deviceRows[0].backend_calls -ne 4 -or $deviceRows[0].evictions -ne 2)){throw 'Independent-request cache contract receipts missing'}
  [void]$records.Add([ordered]@{test=$test;passed=$true;device_receipts=$deviceRows.Count;elapsed_ms=$watch.ElapsedMilliseconds;memory_cache_option='1';private_queue_option='1'})
  [ordered]@{completed=$false;cases=@($records.ToArray())}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
 }
 [ordered]@{completed=$true;npu_qualified=$true;cases=@($records.ToArray());memory_cache_option='1';private_queue_option='1';gpu_memory_cache_enabled=$false;compact_option='1';full_desktop_replay_passed=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
}finally{$env:PATH=$old;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_MEMORY_CACHE=$oldCompact;$env:NR_RENDER_SHARED_QUEUE=$oldQueue}
