$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v112\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$b="$r\builds\clamp-diagnostic-v1";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path $b){throw 'Diagnostic build exists'}
$ErrorActionPreference='Continue'
& cmake -S "$r\vm\clamp-diagnostic-v1" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> "$r\vm\build-clamp-diagnostic-v1.log"
$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_draw_replay_probe nr_registered_texture_test nr_registered_mirror_test nr_registered_compositor_test *>> "$r\vm\build-clamp-diagnostic-v1.log";$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content "$r\vm\build-clamp-diagnostic-v1.log" -Tail 8;if($code){throw 'Diagnostic build failed'}
$sources=[ordered]@{};foreach($f in (Get-ChildItem "$r\vm\clamp-diagnostic-v1" -File)){$sources[$f.Name]=(Get-FileHash $f.FullName).Hash}
[ordered]@{sources=$sources;binaries=@(Get-ChildItem "$b\Release" -Filter "*.exe"|ForEach-Object{@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}});binary_sha256=(Get-FileHash "$b\Release\nr_registered_draw_replay_probe.exe").Hash;runtime_directory="$sdk\libs";runtime=@(Get-ChildItem "$sdk\libs" -Filter '*.dll'|ForEach-Object{@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}});baseline_source='present-umd-v216';shipping_source_changed=$false;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\probe-build-report.json"
