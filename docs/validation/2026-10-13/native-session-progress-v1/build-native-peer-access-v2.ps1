$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-peer-access-v2';$source="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Experiment already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$ErrorActionPreference='Continue'
& cmake -S $source -B $b -A x64 "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 15;if($code){throw 'Loaded DLL test build failed'}
[ordered]@{clean_build=$true;source_sha256=$expected;binary_sha256=@{'nr_native_peer_access.exe'=(Get-FileHash "$b\Release\nr_native_peer_access.exe").Hash};npu_tested=$false;physical_driver_installed=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"

$dwm=@(Get-Process dwm|Where-Object SessionId -gt 0);if($dwm.Count -ne 1){throw 'Expected one compositor for inspection'}
$hostProcess=@(Get-Process WUDFHost|Sort-Object Id)[0];if(!$hostProcess){throw 'No UMDF host for read-only inspection'}
$ErrorActionPreference='Continue';& "$b\Release\nr_native_peer_access.exe" $dwm[0].Id $hostProcess.Id 1> "$r\vm\native-peer-access-v2.stdout.jsonl" 2> "$r\vm\native-peer-access-v2.stderr.log";$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
Get-Content "$r\vm\native-peer-access-v2.stdout.jsonl";if($code){Get-Content "$r\vm\native-peer-access-v2.stderr.log";throw 'DWM read-only inspection failed'}
