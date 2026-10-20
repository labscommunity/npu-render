$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$name='ptl-compiled-turbo-v1';$s="$vm\$name";$b="$r\builds\$name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if(Test-Path "$b/build-report-v2.json"){throw 'Verification already exists'}
$prior=Get-Content "$vm/ptl-worker-v30-qualification/report.json" -Raw|ConvertFrom-Json
if(!$prior.completed -or $prior.passed -or $prior.error -ne 'provider-build failed with exit 1'){throw 'Unexpected preceding failure'}
if([IO.File]::ReadAllText("$vm/ptl-worker-v30-qualification/provider-build.stderr.log") -notmatch 'Expected exactly two cache contracts'){throw 'Unexpected build failure'}
$expected=Get-Content "$vm/$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$s/$($f.Name)").Hash.ToLower() -ne $f.Value){throw 'Provider source changed'}}
$before=@{};foreach($n in @('nr_registered_vertex.lib','nr_registered_provider_cache.lib','nr_registered_provider_cache_test.exe','nr_registered_provider_memory_cache_test.exe')){$before[$n]=(Get-FileHash "$b/Release/$n").Hash.ToLower()}
$env:PATH="$sdk\libs;"+$env:PATH
$junit="$vm/$name-contract-v2.xml";if(Test-Path $junit){throw 'Structured result exists'}
& ctest --test-dir $b -C Release -R "^registered_provider_(cache|memory_cache)_contract$" --no-tests=error --output-on-failure --output-junit $junit *> "$vm/$name-contract-v2.log"
if($LASTEXITCODE){throw 'Provider cache contract failed'}
[xml]$xml=[IO.File]::ReadAllText($junit);$suite=$xml.testsuite;$cases=@($suite.testcase)
if($suite.tests -ne '2' -or $suite.failures -ne '0' -or $suite.disabled -ne '0' -or $cases.Count -ne 2){throw 'Expected two successful CPU contracts'}
$names=@($cases|ForEach-Object {[string]$_.name}|Sort-Object)
if(($names -join ',') -ne 'registered_provider_cache_contract,registered_provider_memory_cache_contract'){throw 'Unexpected test identities'}
foreach($case in $cases){if($case.failure -or $case.error -or $case.skipped -or $case.status -ne 'run'){throw 'Incomplete CPU contract'}}
foreach($n in $before.Keys){if((Get-FileHash "$b/Release/$n").Hash.ToLower() -ne $before[$n]){throw 'Build output changed during validation'}}
$libs=@{};foreach($n in @('nr_registered_vertex.lib','nr_registered_provider_cache.lib')){$libs[$n]=$before[$n]}
[ordered]@{clean_build=$true;source_sha256=$expected;library_sha256=$libs;cpu_cache_contracts_passed=$true;contract_names=$names;contract_report_sha256=(Get-FileHash $junit).Hash.ToLower();build_log_sha256=(Get-FileHash "$vm/build-$name.log").Hash.ToLower();preceding_failure='text summary format mismatch after successful build and tests';source_or_binary_changed=$false;npu_execution=$false;desktop_changed=$false}|ConvertTo-Json -Depth 6|Set-Content "$b/build-report-v2.json"
