$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v109\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$b="$r\builds\compiler-batch-probe-v4";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path $b){throw 'Diagnostic build exists'}
$ErrorActionPreference='Continue';& cmake -S "$r\vm\compiler-batch-probe-v4" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_RENDER_SOURCE=$src\native\windows\registered_lab" "-DNR_RENDER_LIBRARY=$r\builds\present-umd-v212\Release\nr_registered_vertex.lib" "-DNR_CACHE_LIBRARY=$r\builds\present-umd-v212\Release\nr_registered_provider_cache.lib" *> "$r\vm\build-compiler-batch-probe-v4.log";$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> "$r\vm\build-compiler-batch-probe-v4.log";$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content "$r\vm\build-compiler-batch-probe-v4.log" -Tail 7;if($code){throw 'Diagnostic build failed'}

$sources=[ordered]@{};foreach($f in (Get-ChildItem "$r\vm\compiler-batch-probe-v4" -File|Where-Object{$_.Extension -in '.cpp','.hpp','.txt'})){$sources[$f.Name]=(Get-FileHash $f.FullName).Hash}
$binaries=[ordered]@{};foreach($n in 'nr_compositor_batch_probe.exe'){$binaries[$n]=(Get-FileHash "$b\Release\$n").Hash}
$linked=[ordered]@{};foreach($n in 'nr_registered_vertex.lib','nr_registered_provider_cache.lib'){$linked[$n]=(Get-FileHash "$r\builds\present-umd-v212\Release\$n").Hash}
[ordered]@{sources=$sources;binary_sha256=$binaries['nr_compositor_batch_probe.exe'];binary_hashes=$binaries;linked_libraries=$linked;runtime_directory="$sdk\libs";runtime=@(Get-ChildItem "$sdk\libs" -Filter '*.dll'|ForEach-Object{@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}});qualified_renderer='present-umd-v212';desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\probe-build-report.json"
