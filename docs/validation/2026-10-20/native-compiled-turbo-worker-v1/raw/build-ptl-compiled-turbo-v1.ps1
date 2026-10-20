$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='ptl-compiled-turbo-v1';$s="$r\vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$name.log"
if((Test-Path $s) -or (Test-Path $b)){throw 'Provider experiment already exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash.ToLower() -ne $f.Value){throw 'Provider source mismatch'}}
$env:CMAKE_BUILD_PARALLEL_LEVEL='4';$ErrorActionPreference='Continue'
& cmake -S "$s\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_vertex nr_registered_provider_cache nr_registered_provider_cache_test nr_registered_provider_memory_cache_test *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';if($code){Get-Content $log -Tail 15;throw 'Provider build failed'}
$env:PATH="$sdk\libs;"+$env:PATH
& ctest --test-dir $b -C Release -R "^registered_provider_(cache|memory_cache)_contract$" --no-tests=error --output-on-failure *> "$r\vm\$name-contract.log"
if($LASTEXITCODE){throw 'Provider cache contract failed'}
$contracts=[IO.File]::ReadAllText("$r\vm\$name-contract.log")
if($contracts -notmatch '100% tests passed, 0 tests failed out of 2'){throw 'Expected exactly two cache contracts'}
$libs=@{};foreach($n in @('nr_registered_vertex.lib','nr_registered_provider_cache.lib')){$libs[$n]=(Get-FileHash "$b\Release\$n").Hash}
[ordered]@{clean_build=$true;source_sha256=$expected;library_sha256=$libs;cpu_cache_contracts_passed=$true;npu_execution=$false;desktop_changed=$false}|ConvertTo-Json -Depth 6|Set-Content "$b\build-report.json"
Get-Content "$b\build-report.json" -Raw
