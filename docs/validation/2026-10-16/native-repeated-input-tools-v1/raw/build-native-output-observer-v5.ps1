$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-output-observer-v5';$s="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log"
if((Test-Path $s) -or (Test-Path $b)){throw 'Build already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S $s -B $b -A x64 *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 8;if($code){throw 'Presentation probe build failed'}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
[ordered]@{kind='finite-physical-output-observer';source_sha256=$expected;binary_sha256=(Get-FileHash "$b\Release\nr_output_observer.exe").Hash;clean_build=$true;physical_presentation_tested=$false;full_desktop_offload=$false}|ConvertTo-Json -Depth 6|Set-Content "$b\build-report.json"
