$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\input-reuse-v1";$out="$r\vm\input-reuse-v1-qualification-retry";$prior="$r\vm\input-reuse-v1-qualification"
if(Test-Path $out){throw 'Qualification evidence exists'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.immutable_per_draw_inputs -or !$m.prepared_input_contract_tested){throw 'Clean build and CPU input contracts required'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
$records=[Collections.ArrayList]::new()
foreach($backend in 'NPU','GPU'){
 $rows=@(Get-Content "$prior\prepared_inputs-$backend.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json})
 if($rows.Count -ne 2 -or @($rows|Where-Object {!$_.passed -or $_.execution_device -ne $backend}).Count -or ($rows|Measure-Object verified_components -Sum).Sum -ne 6912){throw 'Prior independent input oracle incomplete'}
 if((Get-Item "$prior\prepared_inputs-$backend.stderr.log").Length){throw 'Unexpected prior test stderr'}
 [void]$records.Add([ordered]@{test='prepared_inputs';backend=$backend;passed=$true;verified_components=6912;evidence="input-reuse-v1-qualification/prepared_inputs-$backend.stdout.jsonl";reused_prior_pass=$true})
}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldPolicy=$env:NR_RENDER_PIPELINE_CACHE;$oldCache=$env:NR_RENDER_NPU_CACHE
function Run-Contract([string]$target,[string]$backend,[int]$maximumSeconds,[Collections.ArrayList]$results){
 $clock=[Diagnostics.Stopwatch]::StartNew();$name="$target-$backend"
 $p=Start-Process "$b\Release\nr_registered_${target}_test.exe" -ArgumentList @('--backend',$backend) -RedirectStandardOutput "$out\$name.stdout.jsonl" -RedirectStandardError "$out\$name.stderr.log" -PassThru
 [void]$p.Handle
 try{if(!$p.WaitForExit($maximumSeconds*1000)){$p.Kill();throw "Timed out: $name"};$p.Refresh();if($p.ExitCode){throw "Contract failed: $name exit $($p.ExitCode)"}}finally{$p.Dispose()}
 [void]$results.Add([ordered]@{test=$target;backend=$backend;passed=$true;elapsed_ms=$clock.ElapsedMilliseconds})
 [ordered]@{completed=$false;cases=@($results.ToArray())}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
 "Contract passed: $name"
}
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE=$null;$env:NR_RENDER_PIPELINE_CACHE=$null
 Run-Contract 'texture' 'NPU' 300 $records
 Run-Contract 'loop' 'NPU' 600 $records
 [ordered]@{completed=$true;passed=$true;build='input-reuse-v1';cases=@($records.ToArray());prepared_input_oracle_components_per_device=6912;source_snapshot_and_lifetime_verified=$true;new_draw_refresh_verified=$true;invalid_preparation_revokes_inputs=$true;graph_math_changed=$false;full_trace_replay_tested=$false;performance_improvement_measured=$false;desktop_use_qualified=$false;prior_controller_failure='PowerShell script-scope collection was an ordered dictionary; second successful test could not append metadata. Raw results preserved; no device failure.';original_evidence_preserved=$true}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
 'Prepared-input NPU/GPU contracts and NPU texture/loop regressions passed'
}finally{$env:PATH=$oldPath;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldPolicy;$env:NR_RENDER_NPU_CACHE=$oldCache}
