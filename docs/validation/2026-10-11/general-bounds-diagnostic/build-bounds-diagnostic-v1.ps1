$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$b="$r\builds\bounds-diagnostic-v1";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path $b){throw 'Diagnostic build exists'}
$ErrorActionPreference='Continue';& cmake -S "$r\vm\bounds-diagnostic-v1" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_RENDER_SOURCE=$src\native\windows\registered_lab" *> "$r\vm\build-bounds-diagnostic-v1.log";$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> "$r\vm\build-bounds-diagnostic-v1.log";$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content "$r\vm\build-bounds-diagnostic-v1.log" -Tail 7;if($code){throw 'Diagnostic build failed'}
$env:PATH="$sdk\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205";$runs=@()
foreach($backend in 'CPU','NPU'){foreach($test in 'vertex','texture','loop'){
 $arguments=if($test -eq 'loop'){@($backend)}else{@('--backend',$backend)};$ErrorActionPreference='Continue'
 & "$b\Release\bounds_${test}_test.exe" @arguments *> "$r\vm\bounds-diagnostic-v1-$test-$backend.log";$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
 $runs += [ordered]@{test=$test;backend=$backend;exit_code=$code};$runs|ConvertTo-Json|Set-Content "$r\vm\bounds-diagnostic-v1-runs.json"
 Get-Content "$r\vm\bounds-diagnostic-v1-$test-$backend.log" -Tail 4
 if($code){throw "Bounds diagnostic failed: $test $backend"}
}}
