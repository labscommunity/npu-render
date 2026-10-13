$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$qualified=Get-Content "$r\vm\desktop-worker-v6-qualified.json" -Raw|ConvertFrom-Json
$renderer=Get-Content "$r\vm\component-coverage-v229\report.json" -Raw|ConvertFrom-Json
if(!$renderer.all_checks_passed -or $renderer.build -ne 'present-umd-v229'){throw 'Renderer hardware qualification missing'}
if(!$qualified.all_checks_passed -or $qualified.build -ne 'desktop-worker-v6'){throw 'Full runtime qualification is required'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
$old=Get-ExecutionPolicy -Scope Process;$oldTrace=$env:NR_RENDER_TRACE_REQUESTS;$env:NR_RENDER_TRACE_REQUESTS='1';$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldCache=$env:NR_RENDER_PIPELINE_CACHE;$env:NR_RENDER_SHARED_QUEUE='1';$env:NR_RENDER_PIPELINE_CACHE='expanded'
try {
 Set-ExecutionPolicy -Scope Process Bypass -Force
 & "$r\vm\stream-controller-v121-diagnostic.ps1" -Session $nrLab -BuildDirectory "$r\builds\desktop-worker-v6" -RuntimeDirectory "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" -EvidenceDirectory "$r\vm\indexed-dwm-stream-v121" -GuestActionScript C:\npu-lab\warm-indexed-v121.ps1 -ModelCacheDirectory "$r\vm\model-cache-2026-4-v209" -MaximumRequests 4096 -WorkerMaximumSeconds 1800
} finally {$env:NR_RENDER_TRACE_REQUESTS=$oldTrace;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldCache;Set-ExecutionPolicy -Scope Process $old -Force;Remove-PSSession $nrLab}
