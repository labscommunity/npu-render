$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25'
$out="$r\vm\cache-v118-qualification";$cache="$r\vm\model-cache-v118"
if((Test-Path $out) -or (Test-Path $cache)){throw 'Evidence or cache exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v83\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Finish live desktop NPU stream before qualification'}
New-Item -ItemType Directory $out,$cache|Out-Null
& icacls.exe $cache /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' "$($env:USERNAME):(OI)(CI)F" *> "$out\cache-acl.log"
if($LASTEXITCODE -ne 0){throw 'Private cache ACL failed'}
$env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH";$oldCache=$env:NR_RENDER_NPU_CACHE
try {
 $env:NR_RENDER_NPU_CACHE=$cache;$ErrorActionPreference='Continue'
 foreach($run in 'cold','warm'){
  & "$r\builds\present-umd-v118\Release\nr_registered_draw_replay_probe.exe" --capture "$r\vm\replay-v81-startmenu-cpu\capture\umd-trace\draw-6128.bin" --backend NPU --output "$out\$run.bin" *> "$out\$run.log"
  $code=$LASTEXITCODE;Get-Content "$out\$run.log";if($code -ne 0){throw 'Cache replay failed'}
 }
 $ErrorActionPreference='Stop';$stats=@{}
 foreach($run in 'cold','warm'){
  $stats[$run]=Get-Content "$out\$run.log"|Where-Object {$_ -match '"kind":"model_compile_cache"'}|ConvertFrom-Json
  if($stats[$run].cache_requests -ne $stats[$run].compile_calls -or $stats[$run].cache_status_unavailable -ne 0){throw 'Incomplete cache qualification telemetry'}
  if((Get-FileHash "$out\$run.bin").Hash -ne '967048BDEC4AFFA842185C9E1FFB715AAFD9741FA989A592E5F590DE1313BE87'){throw 'Cached NPU pixels changed'}
 }
 if($stats.warm.compile_calls -lt 1 -or $stats.warm.cache_hits -ne $stats.warm.compile_calls){throw 'Fresh-process warm replay did not load every model from cache'}
 $ErrorActionPreference='Continue'
 & "$r\builds\present-umd-v118\Release\nr_registered_lab_test.exe" NPU *> "$out\driver-callbacks.log"
 $code=$LASTEXITCODE;Get-Content "$out\driver-callbacks.log"|Select-Object -Last 3;if($code -ne 0){throw 'Cached NPU callback checks failed'}
 $env:NR_RENDER_NPU_CACHE='relative-cache'
 & "$r\builds\present-umd-v118\Release\nr_registered_projection_test.exe" NPU *> "$out\invalid-cache.log"
 $code=$LASTEXITCODE;if($code -eq 0 -or (Get-Content "$out\invalid-cache.log" -Raw) -notmatch 'existing absolute directory'){throw 'Invalid cache configuration was not rejected'}
 $ErrorActionPreference='Stop'
 [ordered]@{cold=$stats.cold;warm=$stats.warm;fresh_processes=$true;pixel_equivalence=$true;invalid_cache_rejected=$true;cache_files=@(Get-ChildItem $cache -File -Recurse).Count;cache_bytes=(Get-ChildItem $cache -File -Recurse|Measure-Object Length -Sum).Sum;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Tee-Object "$out\report.json"
}finally{$env:NR_RENDER_NPU_CACHE=$oldCache}
