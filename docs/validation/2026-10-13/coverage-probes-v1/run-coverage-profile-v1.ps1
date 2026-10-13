$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\coverage-profile-v1";$out="$r\vm\coverage-profile-v1-qualification"
if(Test-Path $out){throw 'Profile evidence exists'}
if(@(Get-Process nr_registered_loop_test,nr_registered_fused_coverage_merger_test,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json;if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed){throw 'Build qualification missing'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
New-Item -ItemType Directory $out|Out-Null;$saved=@{};foreach($k in 'PATH','NR_RENDER_NPU_CACHE','NR_RENDER_SHARED_QUEUE','NPU_PRINT_PROFILING','NPU_PROFILING_OUTPUT_FILE','NPU_PROFILING_VERBOSITY'){$saved[$k]=[Environment]::GetEnvironmentVariable($k,'Process')};$records=[Collections.ArrayList]::new()
try {
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE=$null;$env:NPU_PRINT_PROFILING='JSON';$env:NPU_PROFILING_VERBOSITY='HIGH'
 foreach($count in 4096,65536){
  $env:NPU_PROFILING_OUTPUT_FILE="$out\NPU-$count.perfetto.json"
  $p=Start-Process "$b\Release\nr_registered_coverage_profile.exe" -ArgumentList @('NPU',"$count") -RedirectStandardOutput "$out\NPU-$count.stdout.jsonl" -RedirectStandardError "$out\NPU-$count.stderr.log" -PassThru;[void]$p.Handle
  try{if(!$p.WaitForExit(900000)){$p.Kill();throw 'Profile timed out'};$p.Refresh();if($p.ExitCode){throw 'Profile oracle failed'}}finally{$p.Dispose()}
  $rows=@(Get-Content "$out\NPU-$count.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$last=@($rows|Where-Object {$_.total_verified_values});$layers=@($rows|Where-Object {$_.layer_profile})
  if($last.Count -ne 1 -or !$last[0].passed -or $last[0].total_verified_values -ne 12*5*$count -or !$layers.Count){throw 'Incomplete profile oracle receipts'}
  if(!(Test-Path $env:NPU_PROFILING_OUTPUT_FILE)){throw 'NPU task profile not produced'}
  [void]$records.Add([ordered]@{samples=$count;passed=$true;verified_values=$last[0].total_verified_values;layer_entries=$layers.Count;task_profile_sha256=(Get-FileHash $env:NPU_PROFILING_OUTPUT_FILE).Hash;task_profile_bytes=(Get-Item $env:NPU_PROFILING_OUTPUT_FILE).Length})
  [ordered]@{completed=$false;cases=@($records.ToArray())}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
 }
 [ordered]@{completed=$true;passed=$true;cases=@($records.ToArray());profiling_overhead_present=$true;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
}finally{foreach($k in $saved.Keys){[Environment]::SetEnvironmentVariable($k,$saved[$k],'Process')}}
