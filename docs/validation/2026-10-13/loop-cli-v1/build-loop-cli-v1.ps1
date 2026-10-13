$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='loop-cli-v1';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if(Test-Path $b){throw 'Build exists'}
if(@(Get-Process nr_registered_fused_coverage_merger_test,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_loop_test *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 5;if($code){throw 'Loop CLI build failed'}
$old=$env:PATH
try{$env:PATH="$sdk\libs;$old";& ctest --test-dir $b -C Release --output-on-failure -R '^(registered_loop_contract|registered_loop_cli_contract)$' *> "$r\vm\$name-CPU-contracts.log";if($LASTEXITCODE){Get-Content "$r\vm\$name-CPU-contracts.log";throw 'Loop contracts failed'}}finally{$env:PATH=$old}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
[ordered]@{kind='loop-test-cli-validation';source_sha256=$expected;binary_sha256=(Get-FileHash "$b\Release\nr_registered_loop_test.exe").Hash;clean_build=$true;full_cpu_loop_contract_passed=$true;invalid_cli_cases_passed=6;invalid_cli_requires_exit_one_and_explicit_stderr=$true;renderer_changed=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\build-report.json"
'Full CPU loop contract and six invalid CLI cases passed'
