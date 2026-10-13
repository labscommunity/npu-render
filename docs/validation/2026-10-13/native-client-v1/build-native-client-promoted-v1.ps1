$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-client-promoted-v1';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Promoted experiment already exists'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Archive extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 8;if($code){throw 'Promoted client build failed'}
$env:PATH="$sdk\libs;$env:PATH";$env:NR_RENDER_MEMORY_CACHE='0';$env:NR_RENDER_COMPACT_COVERAGE='0';Remove-Item Env:NR_RENDER_SHARED_QUEUE -ErrorAction SilentlyContinue;Remove-Item Env:NR_RENDER_PIPELINE_CACHE -ErrorAction SilentlyContinue
$ErrorActionPreference='Continue'
& ctest --test-dir $b -C Release --output-on-failure --no-tests=error *> "$r\vm\$name-CPU-contracts.log";$code=$LASTEXITCODE
$ErrorActionPreference='Stop';Get-Content "$r\vm\$name-CPU-contracts.log" -Tail 10;if($code){throw 'Promoted CPU contracts failed'}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
[ordered]@{kind='production-native-pipe-client';source_sha256=$expected;binary_sha256=@{'nr_registered_local_indexed_client_probe.exe'=(Get-FileHash "$b\Release\nr_registered_local_indexed_client_probe.exe").Hash;'nr_registered_local_indexed_client_test.exe'=(Get-FileHash "$b\Release\nr_registered_local_indexed_client_test.exe").Hash};clean_build=$true;relevant_cpu_contracts_passed=$true;native_pipe_client=$true;physical_driver_installed=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Promoted client clean build and CPU suite passed'
