$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='ptl-three-texture-diagnostic-v2';$s="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if((Test-Path $s) -or (Test-Path $b)){throw 'Diagnostic exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$libs=@(Get-FileHash "$r\builds\native-routing-v45\Release\nr_registered_vertex.lib","$r\builds\native-routing-v45\Release\nr_registered_provider_cache.lib")
$ErrorActionPreference='Continue'
& cmake -S $s -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_QUALIFIED_SOURCE=$r\vm\native-routing-v45\native\windows\registered_lab" "-DNR_QUALIFIED_BUILD=$r\builds\native-routing-v45" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE};$ErrorActionPreference='Stop';if($code){Get-Content $log -Tail 12;throw 'Diagnostic build failed'}
foreach($lib in $libs){if((Get-FileHash $lib.Path).Hash -ne $lib.Hash){throw 'Library changed'}}
[ordered]@{clean_build=$true;source_sha256=$expected;linked_libraries=$libs;binary_sha256=(Get-FileHash "$b\Release\nr_ptl_four_tap_compare.exe").Hash;rendering_implementation='native-routing-v45 qualified native renderer'}|ConvertTo-Json -Depth 5|Set-Content "$b\build-report.json"
Get-Content "$b\build-report.json" -Raw
