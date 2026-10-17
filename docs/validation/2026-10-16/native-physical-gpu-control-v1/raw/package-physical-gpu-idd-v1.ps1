$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\physical-gpu-idd-v1-native-gpu";$source="$r\vm\physical-gpu-idd-v1";$out="$r\vm\physical-gpu-idd-package-v1";$thumb='3112355AF6B6C6265E9B849662D3E02F6797343A'
if(Test-Path $out){throw 'Package exists'}
$report=Get-Content "$b\build-report.json" -Raw|ConvertFrom-Json;if(!$report.clean_build -or !$report.native_admission_enabled -or $report.cpu_contract_suites -ne 5 -or !$report.hardware_gpu_control -or $report.target_vendor -ne 32902 -or $report.target_device -ne 45184 -or $report.desktop_lease_max_ms -ne 2400000 -or $report.owned_app_lease_max_ms -ne 120000){throw 'Native indirect build unavailable'}
foreach($f in $report.source_sha256.psobject.Properties){if((Get-FileHash "$source\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
if((Get-FileHash "$b\Release\nr_native_indirect_probe.dll").Hash -ne $report.binary_sha256.'nr_native_indirect_probe.dll'){throw 'DLL mismatch'}
$c=Get-Item "Cert:\LocalMachine\My\$thumb";if(!$c.HasPrivateKey -or $c.NotAfter -lt [DateTime]::Now -or @($c.Extensions|Where-Object {$_.Oid.Value -eq '2.5.29.37'}|ForEach-Object {$_.EnhancedKeyUsages}|ForEach-Object {$_.Value}) -notcontains '1.3.6.1.5.5.7.3.3'){throw 'Lab signer unavailable'}
New-Item -ItemType Directory $out|Out-Null
Copy-Item "$b\Release\nr_native_indirect_probe.dll" $out;Copy-Item "$source\native\windows\indirect_lab\nr_native_indirect_probe.inf" $out
Export-Certificate -Cert $c -FilePath "$out\test-signing.cer"|Out-Null
& "$r\dependencies\wdk-tools\infverif.exe" /w "$out\nr_native_indirect_probe.inf" *> "$out\infverif.log";if($LASTEXITCODE){throw 'INF validation failed'}
$sign='C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
& $sign sign /sm /s My /fd SHA256 /sha1 $thumb "$out\nr_native_indirect_probe.dll" *> "$out\sign-dll.log";if($LASTEXITCODE){throw 'DLL signing failed'}
& "$r\dependencies\wdk-tools\Inf2Cat.exe" "/driver:$out" /os:10_GE_X64 *> "$out\catalog.log";if($LASTEXITCODE){throw 'Catalog generation failed'}
& $sign sign /sm /s My /fd SHA256 /sha1 $thumb "$out\nr_native_indirect_probe.cat" *> "$out\sign-catalog.log";if($LASTEXITCODE){throw 'Catalog signing failed'}
$manifest=@(Get-ChildItem $out -File|Where-Object Extension -in '.dll','.cat','.inf','.cer'|ForEach-Object {[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}});$manifest|ConvertTo-Json -Depth 4|Set-Content "$out\sha256.json"
[ordered]@{prepared=$true;installed=$false;hardware_id='ROOT\NPURENDER_NATIVE_INDIRECT_V1';device_group='T8NativeIddV1';version='0.0.1601.0';hardware_gpu_control=$true;target_vendor=32902;target_device=45184;desktop_lease_max_ms=2400000;owned_app_lease_max_ms=120000;admission_protocol=3;signer_thumbprint=$thumb;unsigned_dll_sha256=$report.binary_sha256.'nr_native_indirect_probe.dll';files=$manifest;driver_entry_executed=$false;desktop_offloaded=$false;finished=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
Get-Content "$out\report.json"
