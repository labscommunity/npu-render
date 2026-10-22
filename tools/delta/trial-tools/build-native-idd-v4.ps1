$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-idd-v4';$source="$r\vm\$name"
if(Test-Path $source){throw 'Experiment source exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
foreach($kind in @('native','guest')){
 $b="$r\builds\$name-$kind";$log="$r\vm\build-$name-$kind.log";if(Test-Path $b){throw 'Build exists'}
 $mode=if($kind -eq 'native'){'ON'}else{'OFF'}
 $ErrorActionPreference='Continue'
 & cmake -S "$source\native\windows\indirect_lab" -B $b -A x64 "-DNR_WDK_ROOT=$r\dependencies\wdk-umdf-26100.6584" "-DNR_IDD_NATIVE_PROBE=$mode" *> $log;$code=$LASTEXITCODE
 if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
 if(!$code){& ctest --test-dir $b -C Release --output-on-failure *>> $log;$code=$LASTEXITCODE}
 $ErrorActionPreference='Stop';Get-Content $log -Tail 15;if($code){throw 'Native indirect build/contract failed'}
 $dll=if($kind -eq 'native'){'nr_native_indirect_probe.dll'}else{'nr_indirect_lab.dll'}
 [ordered]@{clean_build=$true;cpu_contract_suites=4;desktop_lease_max_ms=2400000;owned_app_lease_max_ms=120000;native_admission_enabled=($kind -eq 'native');source_sha256=$expected;binary_sha256=@{$dll=(Get-FileHash "$b\Release\$dll").Hash;'nr_idd_lease_contract.exe'=(Get-FileHash "$b\Release\nr_idd_lease_contract.exe").Hash;'nr_native_indirect_admission_test.exe'=(Get-FileHash "$b\Release\nr_native_indirect_admission_test.exe").Hash};driver_installed=$false;physical_desktop_offloaded=$false}|ConvertTo-Json -Depth 6|Set-Content "$b\build-report.json"
}
