$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$qualified=Get-Content "$r\vm\trace-worker-v2-qualified.json" -Raw|ConvertFrom-Json
$renderer=Get-Content "$r\vm\uniform-partial-v224\report.json" -Raw|ConvertFrom-Json
if(!$renderer.all_checks_passed -or $renderer.build -ne 'present-umd-v224'){throw 'Renderer hardware qualification missing'}
if(!$qualified.all_checks_passed -or $qualified.build -ne 'trace-worker-v2'){throw 'Full runtime qualification is required'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
$old=Get-ExecutionPolicy -Scope Process;$oldTrace=$env:NR_RENDER_TRACE_REQUESTS;$env:NR_RENDER_TRACE_REQUESTS='1'
try {
 Set-ExecutionPolicy -Scope Process Bypass -Force
 & "$r\vm\stream-controller-v120-diagnostic.ps1" -Session $nrLab -BuildDirectory "$r\builds\trace-worker-v2" -RuntimeDirectory "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" -EvidenceDirectory "$r\vm\indexed-dwm-stream-v116" -GuestActionScript C:\npu-lab\warm-indexed-v116.ps1 -ModelCacheDirectory "$r\vm\model-cache-2026-4-v209" -MaximumRequests 4096
} finally {$env:NR_RENDER_TRACE_REQUESTS=$oldTrace;Set-ExecutionPolicy -Scope Process $old -Force;Remove-PSSession $nrLab}
