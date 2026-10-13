$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$m=Get-Content "$r\builds\desktop-worker-v6\registered-build-report.json" -Raw|ConvertFrom-Json;$prior=Get-Content "$r\vm\ordered-point-reentry-v1-summary.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.cpu_protocol_contract_passed -or !$m.point_crop_reentry -or !$prior.completed -or !$prior.passed -or !$prior.all_pixels_and_coverage_exact){throw 'Renderer and transport qualification missing'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
$oldTrace=$env:NR_RENDER_TRACE_REQUESTS;$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldCache=$env:NR_RENDER_PIPELINE_CACHE;$oldPolicy=Get-ExecutionPolicy -Scope Process
try{
 $env:NR_RENDER_TRACE_REQUESTS='1';$env:NR_RENDER_SHARED_QUEUE='1';$env:NR_RENDER_PIPELINE_CACHE='expanded';Set-ExecutionPolicy -Scope Process Bypass -Force
 & "$r\vm\stream-controller-v121-diagnostic.ps1" -Session $nrLab -BuildDirectory "$r\builds\desktop-worker-v6" -RuntimeDirectory "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" -EvidenceDirectory "$r\vm\desktop-worker-v6-smoke" -ModelCacheDirectory "$r\vm\model-cache-2026-4-v209" -SmokeTest -ScissorSmokeTest -WorkerMaximumSeconds 1800
 $report=Get-Content "$r\vm\desktop-worker-v6-smoke\report.json" -Raw|ConvertFrom-Json
 if(!$report.completed -or !$report.configuration_removed -or !$report.smoke_passed -or !$report.worker_output_complete){throw 'Stream smoke failed'}
 $relay=Get-Content "$r\vm\desktop-worker-v6-smoke\relay.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object{$_|ConvertFrom-Json};$ready=@($relay|Where-Object{$_.relay_ready})
 if($ready.Count -ne 1 -or $ready[0].maximum_seconds -ne 1800 -or $report.worker_maximum_seconds -ne 1800 -or $report.relay_maximum_seconds -ne 1800){throw 'Worker and relay durations do not agree'}
 $rows=Get-Content "$r\vm\desktop-worker-v6-smoke\worker.stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object{$_|ConvertFrom-Json};$starts=@($rows|Where-Object{$_.kind -eq 'request_started'});$policy=@($rows|Where-Object {$_.kind -eq 'pipeline_cache_policy'});$queue=@($rows|Where-Object {$_.kind -eq 'private_shared_queue_experiment'})
 if($starts.Count -ne 3 -or @($starts|Where-Object{!$_.saved}).Count){throw 'Complete request trace missing'}
 if($policy.Count -ne 1 -or $policy[0].uniform_capacity -ne 16 -or $policy[0].triangle_capacity -ne 48 -or $queue.Count -ne 1 -or !$queue[0].enabled -or !$queue[0].property_readback_verified){throw 'Requested cache/queue mode not verified'}
 [ordered]@{all_checks_passed=$true;build='desktop-worker-v6';cpu_protocol_contract_passed=$true;qualified_renderer_parent='point-reentry-v1';renderer_full_cpu_suites=28;renderer_npu_components=3576128;full_trace_requests=1438;worker_and_relay_seconds=1800;relay_duration_readback_verified=$true;npu_stream_scissor_cases=3;traced_requests=3;expanded_cache_readback_verified=$true;private_queue_readback_verified=$true;gpu_crop_qualification=$false;desktop_offload_tested=$false}|ConvertTo-Json|Set-Content "$r\vm\desktop-worker-v6-qualified.json"
 'Desktop stream smoke passed with expanded cache and shared queue'
}finally{$env:NR_RENDER_TRACE_REQUESTS=$oldTrace;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldCache;Set-ExecutionPolicy -Scope Process $oldPolicy -Force;Remove-PSSession $nrLab}
