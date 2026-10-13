$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-admission-v1';$source="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log"
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_worker,nr_registered_indexed_stream_worker -ErrorAction SilentlyContinue).Count){throw 'Performance replay active'}
if((Test-Path $source) -or (Test-Path $b)){throw 'Admission experiment already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Archive extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Admission source mismatch'}}
$ErrorActionPreference='Continue'
& cmake -S $source -B $b -A x64 *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 10;if($code){throw 'Admission build failed'}
$ErrorActionPreference='Continue'
& ctest --test-dir $b -C Release --output-on-failure --no-tests=error *> "$r\vm\$name-ctest.log";$code=$LASTEXITCODE
& "$b\Release\nr_native_admission_contract.exe" 1> "$r\vm\$name.stdout.jsonl" 2> "$r\vm\$name.stderr.log";$direct=$LASTEXITCODE
$ErrorActionPreference='Stop';Get-Content "$r\vm\$name-ctest.log";Get-Content "$r\vm\$name.stderr.log";if($code -or $direct){throw 'Native admission contracts failed'}
$m=@(Get-Content "$r\vm\$name.stdout.jsonl"|ForEach-Object {$_|ConvertFrom-Json});$done=@($m|Where-Object {$_.native_admission_contract})
if($done.Count -ne 1 -or !$done[0].passed -or $done[0].shape_cases -ne 25 -or $done[0].acl_cases -ne 11 -or $done[0].separate_process_cases -ne 5){throw 'Admission receipts incomplete'}
[ordered]@{passed=$true;clean_build=$true;source_sha256=$expected;binary_sha256=(Get-FileHash "$b\Release\nr_native_admission_contract.exe").Hash;ctest_exit_code=$code;direct_exit_code=$direct;contract=$done[0];driver_installed=$false;registry_or_boot_settings_changed=$false;device_execution_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$r\vm\$name-report.json"
