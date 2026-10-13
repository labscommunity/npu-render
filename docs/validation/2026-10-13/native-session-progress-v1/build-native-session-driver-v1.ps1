$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-session-driver-v1';$source="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Snapshot already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';Push-Location $source
try {
 $ErrorActionPreference='Continue'
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" tools/build_registered_lab.py --wdk-include-root "$r\dependencies\wdk-26100.6584-headers" --build-dir $b *> $log;$code=$LASTEXITCODE
 $ErrorActionPreference='Stop';Get-Content $log -Tail 10|Where-Object {$_ -notmatch '^\{'};if($code){throw 'Production-source build or tests failed'}
}finally{Pop-Location}
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source changed'}}
'Promoted native adapter source built and passed CPU/admission suites'
