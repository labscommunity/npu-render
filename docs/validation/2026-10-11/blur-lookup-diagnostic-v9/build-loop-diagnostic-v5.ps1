$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$b="$r\builds\loop-diagnostic-v5";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path $b){throw 'Diagnostic build exists'}
$ErrorActionPreference='Continue';& cmake -S "$r\vm\loop-diagnostic-v5" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_RENDER_SOURCE=$src\native\windows\registered_lab" *> "$r\vm\build-loop-diagnostic-v5.log";$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> "$r\vm\build-loop-diagnostic-v5.log";$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content "$r\vm\build-loop-diagnostic-v5.log" -Tail 7;if($code){throw 'Diagnostic build failed'}
$env:PATH="$sdk\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205";$ErrorActionPreference='Continue'
& "$b\Release\nr_loop_diagnostic.exe" NPU *> "$r\vm\loop-diagnostic-v5.log";$code=$LASTEXITCODE
[ordered]@{exit_code=$code;binary_sha256=(Get-FileHash "$b\Release\nr_loop_diagnostic.exe").Hash;source_sha256=(Get-FileHash "$r\vm\loop-diagnostic-v5\loop_diagnostic.cpp").Hash}|ConvertTo-Json|Set-Content "$r\vm\loop-diagnostic-v5-report.json"
Get-Content "$r\vm\loop-diagnostic-v5.log" -Tail 20
