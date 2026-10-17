$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='physical-gpu-idd-v1';$source="$r\vm\$name"
if(Test-Path $source){throw 'Experiment source exists'}
& tar -xf "$r\vm\$name.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Extraction failed'}
$expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
foreach($f in $expected.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
$baseline=Get-Content "$r\builds\native-idd-v2-native\build-report.json" -Raw|ConvertFrom-Json
$changed=@($expected.psobject.Properties|Where-Object {$baseline.source_sha256.($_.Name) -ne $_.Value}|ForEach-Object {$_.Name})
$wanted=@('native/windows/indirect_lab/driver.cpp','native/windows/indirect_lab/CMakeLists.txt','native/windows/indirect_lab/nr_native_indirect_probe.inf','native/windows/indirect_lab/physical_gpu_choice_v1.hpp','native/windows/indirect_lab/physical_gpu_probe_v1.cpp')
if(@(Compare-Object ($changed|Sort-Object) ($wanted|Sort-Object)).Count){throw 'Unexpected source change'}
foreach($kind in @('native-gpu','guest-gpu','native-paired','guest-paired')){
 $native=$kind.StartsWith('native');$gpu=$kind.EndsWith('gpu');$b="$r\builds\$name-$kind";$log="$r\vm\build-$name-$kind.log";if(Test-Path $b){throw 'Build exists'}
 $mode=if($native){'ON'}else{'OFF'};$control=if($gpu){'ON'}else{'OFF'}
 $ErrorActionPreference='Continue'
 & cmake -S "$source\native\windows\indirect_lab" -B $b -A x64 "-DNR_WDK_ROOT=$r\dependencies\wdk-umdf-26100.6584" "-DNR_IDD_NATIVE_PROBE=$mode" "-DNR_IDD_HARDWARE_RENDERER_CONTROL=$control" '-DNR_IDD_GPU_VENDOR=0x8086' '-DNR_IDD_GPU_DEVICE=0xb080' *> $log;$code=$LASTEXITCODE
 if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
 if(!$code){& ctest --test-dir $b -C Release --output-on-failure *>> $log;$code=$LASTEXITCODE}
 $ErrorActionPreference='Stop';Get-Content $log -Tail 12;if($code){throw "Hardware control build/contract failed: $kind"}
 $dll=if($native){'nr_native_indirect_probe.dll'}else{'nr_indirect_lab.dll'}
 [ordered]@{clean_build=$true;cpu_contract_suites=5;desktop_lease_max_ms=2400000;owned_app_lease_max_ms=120000;native_admission_enabled=$native;hardware_gpu_control=$gpu;target_vendor=32902;target_device=45184;source_sha256=$expected;binary_sha256=@{$dll=(Get-FileHash "$b\Release\$dll").Hash;'nr_physical_gpu_control_probe.exe'=(Get-FileHash "$b\Release\nr_physical_gpu_control_probe.exe").Hash};driver_installed=$false;physical_desktop_offloaded=$false;tahoma_tested=$false}|ConvertTo-Json -Depth 6|Set-Content "$b\build-report.json"
}
