$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-probe-driver-v2';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Promoted experiment already exists'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Archive extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_native_probe_umd nr_native_probe_ddi_test *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 8;if($code){throw 'Promoted client build failed'}
$env:PATH="$sdk\libs;$env:PATH"
$ErrorActionPreference='Continue'
& "$b\Release\nr_native_probe_ddi_test.exe" --reject-unleased 1> "$r\vm\$name-unleased.stdout.jsonl" 2> "$r\vm\$name-unleased.stderr.log";$code=$LASTEXITCODE
$ErrorActionPreference='Stop';if($code){throw 'Unleased native probe did not reject cleanly'}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
[ordered]@{kind='uninstalled-native-probe-driver';source_sha256=$expected;binary_sha256=@{'nr_native_probe_ddi_test.exe'=(Get-FileHash "$b\Release\nr_native_probe_ddi_test.exe").Hash;'nr_native_probe_umd.dll'=(Get-FileHash "$b\Release\nr_native_probe_umd.dll").Hash};clean_build=$true;unleased_consumer_rejected=$true;default_guest_guard_preserved=$true;physical_driver_installed=$false;npu_component_tested=$false;desktop_use_qualified=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
'Native probe target built; unleased admission rejected'
