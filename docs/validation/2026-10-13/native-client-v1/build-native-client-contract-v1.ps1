$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-pipe-client-contract-v1';$source="$r\vm\$name";$b="$r\builds\$name";$client="$r\vm\native-pipe-client-v1\native\windows\registered_lab";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Contract experiment already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Archive extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Contract source mismatch'}}
$parent=Get-Content "$r\builds\native-pipe-client-v1\registered-build-report.json" -Raw|ConvertFrom-Json
foreach($f in $parent.source_sha256.psobject.Properties){if((Get-FileHash "$r\vm\native-pipe-client-v1\$($f.Name)").Hash -ne $f.Value){throw 'Qualified client source mismatch'}}
$ErrorActionPreference='Continue'
& cmake -S $source -B $b -A x64 "-DNR_CLIENT_SOURCE=$client" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 8;if($code){throw 'Native client contract build failed'}
$env:CTEST_OUTPUT_ON_FAILURE='1';$ErrorActionPreference='Continue'
& ctest --test-dir $b -C Release --output-on-failure --no-tests=error *> "$r\vm\$name-ctest.log";$code=$LASTEXITCODE
& "$b\Release\nr_local_client_contract.exe" 1> "$r\vm\$name.stdout.jsonl" 2> "$r\vm\$name.stderr.log";$direct=$LASTEXITCODE
$ErrorActionPreference='Stop';Get-Content "$r\vm\$name-ctest.log";if($code -or $direct){throw 'Native client contracts failed'}
$m=@(Get-Content "$r\vm\$name.stdout.jsonl"|ForEach-Object {$_|ConvertFrom-Json});if($m.Count -ne 12 -or !$m[-1].native_client_contract -or !$m[-1].passed -or $m[-1].cases -ne 15){throw 'Missing contract receipts'}
[ordered]@{passed=$true;clean_build=$true;test_cases=15;source_sha256=$expected;qualified_client_header_sha256=(Get-FileHash "$client\local_indexed_client.hpp").Hash;binary_sha256=(Get-FileHash "$b\Release\nr_local_client_contract.exe").Hash;ctest_exit_code=$code;direct_exit_code=$direct;device_execution_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$r\vm\$name-report.json"
