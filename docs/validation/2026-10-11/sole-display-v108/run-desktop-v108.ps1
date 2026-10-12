$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$qualified=Get-Content "$r\vm\attribute-v210\report.json" -Raw|ConvertFrom-Json
if(!$qualified.all_checks_passed -or $qualified.build -ne 'present-umd-v210'){throw 'Full runtime qualification is required'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
$old=Get-ExecutionPolicy -Scope Process
try {
 Set-ExecutionPolicy -Scope Process Bypass -Force
 & "$r\vm\run_registered_indexed_stream_cache_v118.ps1" -Session $nrLab -BuildDirectory "$r\builds\present-umd-v210" -RuntimeDirectory "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" -EvidenceDirectory "$r\vm\indexed-dwm-stream-v108" -GuestActionScript C:\npu-lab\warm-indexed-v108.ps1 -ModelCacheDirectory "$r\vm\model-cache-2026-4-v209" -MaximumRequests 4096
} finally {Set-ExecutionPolicy -Scope Process $old -Force;Remove-PSSession $nrLab}
