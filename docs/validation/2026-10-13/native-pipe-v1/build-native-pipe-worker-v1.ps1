$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-pipe-worker-v1';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if(Test-Path $b){throw 'Build exists'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
$parent=Get-Content "$r\builds\memory-cache-v2\registered-build-report.json" -Raw|ConvertFrom-Json;$q=Get-Content "$r\vm\memory-cache-v2-chain.json" -Raw|ConvertFrom-Json
if(!$parent.clean_build -or !$parent.relevant_cpu_contracts_passed -or !$q.completed -or !$q.passed){throw 'Qualified renderer parent required'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
foreach($f in $parent.source_sha256.psobject.Properties){if($f.Name -ne 'native\windows\registered_lab\CMakeLists.txt' -and $expected.($f.Name) -ne $f.Value){throw 'Renderer source differs from qualified parent'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_local_indexed_worker *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 6;if($code){throw 'Local pipe worker build failed'}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
[ordered]@{kind='private-local-npu-pipe-worker';parent='memory-cache-v2';source_sha256=$expected;binary_sha256=@{'nr_registered_local_indexed_worker.exe'=(Get-FileHash "$b\Release\nr_registered_local_indexed_worker.exe").Hash};clean_build=$true;renderer_sources_equal_qualified_parent=$true;parent_cpu_contracts_passed=$true;new_worker_protocol_qualified=$false;native_pipe_worker=$true;compiled_memory_cache_opt_in=$true;compiled_memory_cache_capacity=64;private_queue_property_opt_in=$true;expanded_pipeline_capacity_opt_in=$true;filesystem_replay_maximum_seconds=1800;guest_driver_installed=$false;physical_driver_installed=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Local pipe worker clean build completed'
