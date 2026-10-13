$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='trace-worker-v4';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if(Test-Path $b){throw 'Diagnostic build exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v118\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_indexed_stream_worker nr_registered_indexed_stream_relay nr_registered_indexed_exchange_test nr_registered_indexed_worker nr_registered_stream_duration_test nr_registered_fifo_dispatch_test *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 6;if($code){throw 'Diagnostic build failed'}
$env:PATH="$sdk\libs;$env:PATH";& "$b\Release\nr_registered_indexed_exchange_test.exe" *> "$r\vm\$name-CPU-protocol.log";if($LASTEXITCODE){throw 'Protocol contract failed'}
foreach($target in 'nr_registered_stream_duration_test','nr_registered_fifo_dispatch_test'){
 & "$b\Release\$target.exe" *> "$r\vm\$name-$target.log";if($LASTEXITCODE){throw 'Relay duration/FIFO contract failed'}
}
$hashes=[ordered]@{};foreach($f in $expected.psobject.Properties){$actual=(Get-FileHash "$source\$($f.Name)").Hash;if($actual -ne $f.Value){throw 'Source changed during build'};$hashes[$f.Name]=$actual}
$bins=[ordered]@{};Get-ChildItem "$b\Release" -Filter '*.exe'|ForEach-Object{$bins[$_.Name]=(Get-FileHash $_.FullName).Hash}
[ordered]@{kind='host-stream-diagnostic-build';base_source_commit='a7c07e95d28368f1043dd4d5ba82682cf3a68e88';renderer_build='present-umd-v225';source_root=$source;source_sha256=$hashes;binary_sha256=$bins;clean_build=$true;cpu_protocol_contract_passed=$true;renderer_math_changed=$false;cache_policy_changed=$false;guest_umd_built=$false;worker_duration_configurable=$true;worker_maximum_seconds=1800;relay_duration_configurable=$true;relay_maximum_seconds=1800;relay_duration_contract_passed=$true;fifo_contract_passed=$true;trace_maximum_bytes=8589934592;npu_execution_tested=$false;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Host diagnostic build and protocol contract passed'
