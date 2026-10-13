$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\point-reentry-v1";$out="$r\vm\point-reentry-v1-qualification"
if(Test-Path $out){throw 'Qualification evidence exists'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.point_crop_reentry -or $m.immutable_per_draw_inputs){throw 'Isolated crop build qualification missing'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldPolicy=$env:NR_RENDER_PIPELINE_CACHE;$records=[Collections.ArrayList]::new()
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE=$null;$env:NR_RENDER_PIPELINE_CACHE=$null
 foreach($backend in 'NPU','GPU'){
  $clock=[Diagnostics.Stopwatch]::StartNew();$p=Start-Process "$b\Release\nr_registered_point_texture_draw_test.exe" -ArgumentList @($backend) -RedirectStandardOutput "$out\point-$backend.stdout.jsonl" -RedirectStandardError "$out\point-$backend.stderr.log" -PassThru
  [void]$p.Handle;try{if(!$p.WaitForExit(180000)){$p.Kill();throw 'Point oracle timed out'};$p.Refresh();if($p.ExitCode){throw "Point oracle failed: $backend"}}finally{$p.Dispose()}
  $rows=@(Get-Content "$out\point-$backend.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$last=@($rows|Where-Object {$null -ne $_.verified_components});$adaptive=@($rows|Where-Object {$_.adaptive_point_texture})
  if($last.Count -ne 1 -or $last[0].verified_components -ne 3576128 -or $last[0].invalid_draws_rejected -ne 16 -or $adaptive.Count -ne 18){throw 'Incomplete independent oracle'}
  foreach($row in $rows){if($row.execution_device -and !$row.execution_device.StartsWith($backend)){throw 'Backend mismatch'}}
  foreach($row in $adaptive){if($row.frame -in @(0,2,6,8)){if($row.provider_requests -ne 3){throw 'Crop was not readmitted'}}elseif($row.provider_requests -le 3){throw 'General draw unexpectedly admitted'}}
  [void]$records.Add([ordered]@{backend=$backend;passed=$true;verified_components=$last[0].verified_components;invalid_draws_rejected=$last[0].invalid_draws_rejected;adaptive_frames=$adaptive.Count;elapsed_ms=$clock.ElapsedMilliseconds})
  [ordered]@{completed=$false;cases=@($records.ToArray())}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
  "Point crop transition oracle passed: $backend"
 }
 [ordered]@{completed=$true;passed=$true;build='point-reentry-v1';cases=@($records.ToArray());independent_scalar_oracle=$true;changing_source_and_background=$true;consecutive_general_draws_tested=$true;full_partial_scaled_transitions_tested=$true;full_trace_replay_tested=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldPolicy}
