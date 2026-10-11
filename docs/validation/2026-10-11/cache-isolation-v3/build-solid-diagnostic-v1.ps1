$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$b="$r\builds\solid-diagnostic-v1";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path $b){throw 'Diagnostic build exists'}
$ErrorActionPreference='Continue';& cmake -S "$r\vm\solid-diagnostic-v1" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_RENDER_SOURCE=$src\native\windows\registered_lab" *> "$r\vm\build-solid-diagnostic-v1.log";$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> "$r\vm\build-solid-diagnostic-v1.log";$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content "$r\vm\build-solid-diagnostic-v1.log" -Tail 7;if($code){throw 'Diagnostic build failed'}
