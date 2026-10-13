$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-service-peer-v2';$source="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Promoted experiment already exists'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Archive extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_local_indexed_worker nr_native_service_probe *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 8;if($code){throw 'Promoted client build failed'}
[ordered]@{clean_build=$true;source_sha256=$expected;binary_sha256=@{'nr_native_service_probe.exe'=(Get-FileHash "$b\Release\nr_native_service_probe.exe").Hash;'nr_registered_local_indexed_worker.exe'=(Get-FileHash "$b\Release\nr_registered_local_indexed_worker.exe").Hash};desktop_admission_performed=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
