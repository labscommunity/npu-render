$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='coverage-merger-v1';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if(Test-Path $b){throw 'Diagnostic build exists'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_raster_test nr_registered_output_merger_test nr_registered_fused_coverage_merger_test *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 6;if($code){throw 'Fusion experiment build failed'}
$old=$env:PATH
try{$env:PATH="$sdk\libs;$old";& ctest --test-dir $b -C Release --output-on-failure -R '^(registered_raster_contract|registered_output_merger_contract|registered_fused_coverage_merger_contract)$' *> "$r\vm\$name-CPU-contracts.log";if($LASTEXITCODE){throw 'Coverage/merger CPU contracts failed'}}finally{$env:PATH=$old}
$hashes=[ordered]@{};foreach($f in $expected.psobject.Properties){$actual=(Get-FileHash "$source\$($f.Name)").Hash;if($actual -ne $f.Value){throw 'Source changed during build'};$hashes[$f.Name]=$actual}
$bins=[ordered]@{};Get-ChildItem "$b\Release" -Filter '*.exe'|ForEach-Object{$bins[$_.Name]=(Get-FileHash $_.FullName).Hash}
[ordered]@{kind='isolated-coverage-merger-experiment';parent='queue-worker-v4';source_sha256=$hashes;binary_sha256=$bins;clean_build=$true;relevant_cpu_contracts_passed=$true;renderer_pipeline_changed=$false;standalone_experiment_only=$true;graph_factories_extracted=$true;npu_execution_tested=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Isolated coverage/merger build and three CPU suites passed'
