$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-auto-interactive-v4';$source="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log"
if((Test-Path $source) -or (Test-Path $b)){throw 'Experiment already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$ErrorActionPreference='Continue'
& cmake -S $source -B $b -A x64 "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 15;if($code){throw 'Loaded DLL test build failed'}
& ctest --test-dir $b -C Release --output-on-failure *> "$b\contract.log";if($LASTEXITCODE){Get-Content "$b\contract.log";throw 'Lease contract failed'}
$hashes=@{};foreach($exe in @('nr_auto_interactive_broker.exe','nr_auto_interactive_consumer.exe','nr_auto_interactive_contract.exe')){$hashes[$exe]=(Get-FileHash "$b\Release\$exe").Hash}
[ordered]@{clean_build=$true;source_sha256=$expected;binary_sha256=$hashes;contract_passed=$true;npu_tested=$false;physical_driver_installed=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\registered-build-report.json"
Get-Content "$b\registered-build-report.json" -Raw
