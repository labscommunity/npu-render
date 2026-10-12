$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$m=Get-Content "$r\builds\trace-worker-v1\registered-build-report.json" -Raw|ConvertFrom-Json;if(!$m.clean_build -or !$m.cpu_protocol_contract_passed -or $m.renderer_math_changed -or $m.cache_policy_changed){throw 'Diagnostic build qualification missing'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
$oldTrace=$env:NR_RENDER_TRACE_REQUESTS;$env:NR_RENDER_TRACE_REQUESTS='1';$oldPolicy=Get-ExecutionPolicy -Scope Process
try{
 Set-ExecutionPolicy -Scope Process Bypass -Force
 & "$r\vm\stream-controller-v119.ps1" -Session $nrLab -BuildDirectory "$r\builds\trace-worker-v1" -RuntimeDirectory "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" -EvidenceDirectory "$r\vm\trace-worker-v1-smoke" -ModelCacheDirectory "$r\vm\model-cache-2026-4-v209" -SmokeTest -ScissorSmokeTest
 $report=Get-Content "$r\vm\trace-worker-v1-smoke\report.json" -Raw|ConvertFrom-Json
 if(!$report.completed -or !$report.configuration_removed -or !$report.smoke_passed -or !$report.worker_output_complete){throw 'Trace smoke failed'}
 $rows=Get-Content "$r\vm\trace-worker-v1-smoke\worker.stdout.jsonl"|ForEach-Object{$_|ConvertFrom-Json};$starts=@($rows|Where-Object{$_.kind -eq 'request_started'})
 if($starts.Count -ne 3 -or @($starts|Where-Object{!$_.saved}).Count){throw 'Complete request trace missing'}
 $files=@(Get-ChildItem "$r\vm\trace-worker-v1-smoke\trace-request-*.bin")
 if($files.Count -ne 3){throw 'Unexpected trace file count'}
 [ordered]@{all_checks_passed=$true;build='trace-worker-v1';cpu_protocol_contract_passed=$true;npu_stream_scissor_cases=3;traced_requests=3;renderer_math_changed=$false;cache_policy_changed=$false;provider_runtime_failure_path_tested=$false;desktop_offload_tested=$false}|ConvertTo-Json|Tee-Object "$r\vm\trace-worker-v1-qualified.json"
}finally{$env:NR_RENDER_TRACE_REQUESTS=$oldTrace;Set-ExecutionPolicy -Scope Process $oldPolicy -Force;Remove-PSSession $nrLab}
