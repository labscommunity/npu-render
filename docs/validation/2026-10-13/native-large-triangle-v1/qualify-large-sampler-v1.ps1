$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='large-sampler-v1';$s="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
if((Test-Path $s) -or (Test-Path $b)){throw 'Diagnostic exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$libs=@(Get-FileHash "$r\builds\native-routing-v9\Release\nr_registered_vertex.lib","$r\builds\native-routing-v9\Release\nr_registered_provider_cache.lib")
$ErrorActionPreference='Continue'
& cmake -S $s -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_QUALIFIED_SOURCE=$r\vm\native-routing-v9\native\windows\registered_lab" "-DNR_QUALIFIED_BUILD=$r\builds\native-routing-v9" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE};$ErrorActionPreference='Stop';if($code){Get-Content $log -Tail 12;throw 'Diagnostic build failed'}
foreach($lib in $libs){if((Get-FileHash $lib.Path).Hash -ne $lib.Hash){throw 'Library changed'}}
[ordered]@{clean_build=$true;source_sha256=$expected;linked_libraries=$libs;binary_sha256=(Get-FileHash "$b\Release\nr_large_sampler_test.exe").Hash;rendering_implementation='native-routing-v9 general segmented sampler'}|ConvertTo-Json -Depth 5|Set-Content "$b\build-report.json"
$env:PATH="$sdk\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE='1';$env:NR_RENDER_MEMORY_CACHE='1';$env:NR_RENDER_COMPACT_COVERAGE='0'
$results=@()
foreach($backend in @('CPU','NPU')){
 $p=Start-Process "$b\Release\nr_large_sampler_test.exe" -ArgumentList @($backend) -PassThru -NoNewWindow -RedirectStandardOutput "$s\$backend.stdout.jsonl" -RedirectStandardError "$s\$backend.stderr.log";[void]$p.Handle
 if(!$p.WaitForExit(60000)){$p.Kill();$p.WaitForExit();throw 'Diagnostic timed out'};$p.Refresh();$code=$p.ExitCode;$p.Dispose();$results+=@{backend=$backend;exit_code=$code;pixel_verification_passed=($code -eq 0)}
 Get-Content "$s\$backend.stdout.jsonl";Get-Content "$s\$backend.stderr.log"
};if(@($results|Where-Object exit_code -ne 0).Count){$results|ConvertTo-Json|Set-Content "$s\report.json";throw 'Large sampler regression failed'};$results|ConvertTo-Json|Set-Content "$s\report.json"
