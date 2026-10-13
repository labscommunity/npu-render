$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='point-reentry-v1';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if(Test-Path $b){throw 'Diagnostic build exists'}
$comparison=Get-Content "$r\vm\ordered-input-reuse-v1-summary.json" -Raw|ConvertFrom-Json;if(!$comparison.completed){throw 'Queue comparison must finish before another build'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v120\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop trial active'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another NPU renderer is running'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 6;if($code){throw 'Diagnostic build failed'}
$env:PATH="$sdk\libs;$env:PATH";& "$b\Release\nr_registered_indexed_exchange_test.exe" *> "$r\vm\$name-CPU-protocol.log";if($LASTEXITCODE){throw 'Protocol contract failed'}
& ctest --test-dir $b -C Release --output-on-failure *> "$r\vm\$name-CPU-contracts.log"
if($LASTEXITCODE){throw 'Relevant CPU contracts failed'}
$hashes=[ordered]@{};foreach($f in $expected.psobject.Properties){$actual=(Get-FileHash "$source\$($f.Name)").Hash;if($actual -ne $f.Value){throw 'Source changed during build'};$hashes[$f.Name]=$actual}
$bins=[ordered]@{};Get-ChildItem "$b\Release" -Filter '*.exe'|ForEach-Object{$bins[$_.Name]=(Get-FileHash $_.FullName).Hash}
[ordered]@{kind='host-stream-diagnostic-build';base_source_commit='233d03880981cc7309a4cd27c163acc6ce36a8b1';renderer_build='present-umd-v229';source_root=$source;source_sha256=$hashes;binary_sha256=$bins;clean_build=$true;cpu_protocol_contract_passed=$true;relevant_cpu_contracts_passed=$true;renderer_math_changed=$false;point_crop_dispatch_changed=$true;per_request_compile_telemetry=$true;point_crop_reentry=$true;immutable_per_draw_inputs=$false;cache_policy_changed=$true;expanded_pipeline_capacity_opt_in=$true;private_queue_property_opt_in=$true;filesystem_replay_maximum_seconds=1800;guest_umd_built=$false;worker_duration_configurable=$true;worker_maximum_seconds=1800;trace_maximum_bytes=8589934592;npu_execution_tested=$false;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Host diagnostic build and protocol contract passed'
