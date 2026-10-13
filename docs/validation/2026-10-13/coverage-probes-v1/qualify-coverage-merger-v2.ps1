$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\coverage-merger-v2";$out="$r\vm\coverage-merger-v2-qualification"
if(Test-Path $out){throw 'Evidence exists'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json;if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.standalone_experiment_only){throw 'Build qualification missing'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldPolicy=$env:NR_RENDER_PIPELINE_CACHE;$records=[Collections.ArrayList]::new()
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE=$null;$env:NR_RENDER_PIPELINE_CACHE=$null
 foreach($backend in 'NPU','GPU'){
  $watch=[Diagnostics.Stopwatch]::StartNew();$p=Start-Process "$b\Release\nr_registered_fused_coverage_merger_test.exe" -ArgumentList @($backend) -RedirectStandardOutput "$out\$backend.stdout.jsonl" -RedirectStandardError "$out\$backend.stderr.log" -PassThru
  [void]$p.Handle;try{if(!$p.WaitForExit(900000)){$p.Kill();throw 'Fusion oracle timed out'};$p.Refresh();if($p.ExitCode){throw "Fusion oracle failed: $backend"}}finally{$p.Dispose()}
  $rows=@(Get-Content "$out\$backend.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$oracle=@($rows|Where-Object {$_.fused_scalar_oracle});$last=@($rows|Where-Object {$_.total_verified_values});$profile=@($rows|Where-Object {$_.profile})
  if($oracle.Count -ne 1 -or $oracle[0].verified_values -ne 35840 -or $last.Count -ne 1 -or !$last[0].passed -or $last[0].total_verified_values -ne 281600 -or $profile.Count -ne 12){throw 'Incomplete fusion oracle receipts'}
  foreach($row in $rows){if($row.execution_device -and $row.execution_device -ne $backend){throw 'Wrong reported backend'}}
  [void]$records.Add([ordered]@{backend=$backend;passed=$true;verified_values=281600;elapsed_ms=$watch.ElapsedMilliseconds;profile=$profile})
  [ordered]@{completed=$false;cases=@($records.ToArray())}|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"
 }
 [ordered]@{completed=$true;passed=$true;cases=@($records.ToArray());independent_pixel_and_coverage_oracles=$true;alternating_profile_order=$true;profile_includes_host_io_and_validation=$true;runtime_renderer_integrated=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldPolicy}
