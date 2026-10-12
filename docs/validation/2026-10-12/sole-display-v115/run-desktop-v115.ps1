$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$qualified=Get-Content "$r\vm\trace-worker-v1-qualified.json" -Raw|ConvertFrom-Json
if(!$qualified.all_checks_passed -or $qualified.build -ne 'trace-worker-v1'){throw 'Full runtime qualification is required'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
$old=Get-ExecutionPolicy -Scope Process;$oldTrace=$env:NR_RENDER_TRACE_REQUESTS;$env:NR_RENDER_TRACE_REQUESTS='1'
try {
 Set-ExecutionPolicy -Scope Process Bypass -Force
 & "$r\vm\stream-controller-v119.ps1" -Session $nrLab -BuildDirectory "$r\builds\trace-worker-v1" -RuntimeDirectory "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" -EvidenceDirectory "$r\vm\indexed-dwm-stream-v115" -GuestActionScript C:\npu-lab\warm-indexed-v115.ps1 -ModelCacheDirectory "$r\vm\model-cache-2026-4-v209" -MaximumRequests 4096
} finally {$env:NR_RENDER_TRACE_REQUESTS=$oldTrace;Set-ExecutionPolicy -Scope Process $old -Force;Remove-PSSession $nrLab}
