$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='memory-cache-v2';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if(Test-Path $b){throw 'Build exists'}
if(@(Get-Process nr_registered_provider_memory_cache_test,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 6;if($code){throw 'Runtime build failed'}
$old=$env:PATH;$oldCompact=$env:NR_RENDER_MEMORY_CACHE
try{$env:PATH="$sdk\libs;$old";$env:NR_RENDER_MEMORY_CACHE=$null;& ctest --test-dir $b -C Release --output-on-failure *> "$r\vm\$name-CPU-contracts.log";if($LASTEXITCODE){Get-Content "$r\vm\$name-CPU-contracts.log" -Tail 30;throw 'CPU contracts failed'}}finally{$env:PATH=$old;$env:NR_RENDER_MEMORY_CACHE=$oldCompact}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
$bins=[ordered]@{};Get-ChildItem "$b\Release" -Filter '*.exe'|ForEach-Object{$bins[$_.Name]=(Get-FileHash $_.FullName).Hash}
[ordered]@{kind='isolated-opt-in-compiled-memory-cache';parent='memory-cache-v1';renderer_base='point-reentry-v1';worker_openvino_headers_linked=$true;source_sha256=$expected;binary_sha256=$bins;clean_build=$true;relevant_cpu_contracts_passed=$true;compiled_memory_cache_opt_in=$true;compiled_memory_cache_capacity=64;execution_timer='std::chrono::steady_clock';exact_binding_names_in_cache_key=$true;point_crop_reentry=$true;private_queue_property_opt_in=$true;expanded_pipeline_capacity_opt_in=$true;filesystem_replay_maximum_seconds=1800;gpu_memory_cache_enabled=$false;guest_driver_installed=$false;npu_qualified=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Memory cache clean build and CPU suites passed'
