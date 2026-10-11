$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$b="$r\builds\vertex-id-diagnostic-v1";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path $b){throw 'Diagnostic build exists'}
$ErrorActionPreference='Continue';& cmake -S "$r\vm\vertex-id-diagnostic-v1" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_RENDER_SOURCE=$src\native\windows\registered_lab" *> "$r\vm\build-vertex-id-diagnostic-v1.log";$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> "$r\vm\build-vertex-id-diagnostic-v1.log";$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content "$r\vm\build-vertex-id-diagnostic-v1.log" -Tail 7;if($code){throw 'Diagnostic build failed'}
$env:PATH="$sdk\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205";$ErrorActionPreference='Continue'
& "$b\Release\nr_vertex_id_diagnostic.exe" NPU *> "$r\vm\vertex-id-diagnostic-v1.log";$code=$LASTEXITCODE
[ordered]@{exit_code=$code;binary_sha256=(Get-FileHash "$b\Release\nr_vertex_id_diagnostic.exe").Hash;source_sha256=(Get-FileHash "$r\vm\vertex-id-diagnostic-v1\vertex_id_diagnostic.cpp").Hash}|ConvertTo-Json|Set-Content "$r\vm\vertex-id-diagnostic-v1-report.json"
Get-Content "$r\vm\vertex-id-diagnostic-v1.log" -Tail 20
