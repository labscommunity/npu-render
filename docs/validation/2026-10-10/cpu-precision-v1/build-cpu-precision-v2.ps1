$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$out="$r\builds\cpu-precision-v2";$project="$r\vm\cpu-precision-v2-source"
if((Test-Path $out) -or (Test-Path $project)){throw 'Evidence exists'}
New-Item -ItemType Directory $project|Out-Null;Copy-Item "$r\vm\cpu-precision-v2-CMakeLists.txt" "$project\CMakeLists.txt"
$oldPath=$env:PATH
try {
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH"
 & cmake -S $project -B $out -A x64 "-DNR_SOURCE_ROOT=$src" "-DOpenVINO_DIR=$src\venv\Lib\site-packages\openvino\cmake" *> "$r\vm\cpu-precision-v2-configure.log";if($LASTEXITCODE){throw 'Configure failed'}
 & cmake --build $out --config Release --clean-first *> "$r\vm\cpu-precision-v2-build.log";if($LASTEXITCODE){Get-Content "$r\vm\cpu-precision-v2-build.log" -Tail 20;throw 'Build failed'}
 & ctest --test-dir $out -C Release --verbose *> "$r\vm\cpu-precision-v2-test.log";if($LASTEXITCODE){Get-Content "$r\vm\cpu-precision-v2-test.log" -Tail 35;throw 'Tests failed'}
 Get-Content "$r\vm\cpu-precision-v2-test.log" -Tail 35
} finally {$env:PATH=$oldPath}
