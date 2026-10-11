$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\bounds-diagnostic-v1";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$out="$r\vm\bounds-diagnostic-qualification-v2"
if(Test-Path $out){throw 'Evidence exists'};New-Item -ItemType Directory $out|Out-Null
$env:PATH="$sdk\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205";$runs=@()
foreach($backend in 'CPU','NPU'){foreach($test in 'vertex','texture','loop'){
 $ErrorActionPreference='Continue'
 if($test -eq 'loop'){& "$b\Release\bounds_loop_test.exe" $backend *> "$out\$test-$backend.log"}else{& "$b\Release\bounds_${test}_test.exe" --backend $backend *> "$out\$test-$backend.log"}
 $code=$LASTEXITCODE;$ErrorActionPreference='Stop';$runs += [ordered]@{test=$test;backend=$backend;exit_code=$code};$runs|ConvertTo-Json|Set-Content "$out\runs.json"
 Get-Content "$out\$test-$backend.log" -Tail 4
 if($code){throw "Bounds diagnostic failed: $test $backend"}
}}
