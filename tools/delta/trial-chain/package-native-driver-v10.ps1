$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-driver-package-v33";$thumb='3112355AF6B6C6265E9B849662D3E02F6797343A'
if(Test-Path $out){throw 'Package evidence exists'}
$build=Get-Content "$r\builds\native-tree-v32\build-report.json" -Raw|ConvertFrom-Json;if(!$build.clean_build){throw 'Clean UMD build required'}
$umd="$r\builds\native-tree-v32\Release\nr_native_probe_umd.dll";$kernel="$r\vm\native-driver-package-v19\nr_render_kmd.sys"
$previous=Get-Content "$r\vm\native-driver-package-v19\sha256.json" -Raw|ConvertFrom-Json
if((Get-FileHash $umd).Hash -ne $build.binary_sha256.'nr_native_probe_umd.dll' -or (Get-FileHash $kernel).Hash -ne ($previous|Where-Object name -eq 'nr_render_kmd.sys').sha256){throw 'Qualified binary mismatch'}
$c=Get-Item "Cert:\LocalMachine\My\$thumb";if(!$c.HasPrivateKey -or $c.NotAfter -lt [DateTime]::Now){throw 'Lab code signing identity unavailable'}
New-Item -ItemType Directory $out|Out-Null
Copy-Item $umd $out;Copy-Item $kernel $out
Export-Certificate -Cert $c -FilePath "$out\test-signing.cer"|Out-Null
(Get-Content "$r\vm\native-driver-package-v19\nr_native_probe.inf" -Raw).Replace('DriverVer=10/16/2026,0.0.1319.0','DriverVer=10/20/2026,0.0.1333.0')|Set-Content "$out\nr_native_probe.inf" -Encoding ascii -NoNewline
if(!(Select-String -Path "$out\nr_native_probe.inf" -Pattern '0.0.1333.0' -Quiet)){throw 'INF version substitution failed'}
& "$r\dependencies\wdk-tools\infverif.exe" /w "$out\nr_native_probe.inf" *> "$out\infverif.log";if($LASTEXITCODE){throw 'INF validation failed'}
$sign='C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
& $sign sign /sm /s My /fd SHA256 /sha1 $thumb "$out\nr_native_probe_umd.dll" *> "$out\sign-umd.log";if($LASTEXITCODE){throw 'UMD signing failed'}
& "$r\dependencies\wdk-tools\Inf2Cat.exe" "/driver:$out" /os:10_GE_X64 *> "$out\catalog.log";if($LASTEXITCODE){throw 'Catalog generation failed'}
& $sign sign /sm /s My /fd SHA256 /sha1 $thumb "$out\nr_native_probe.cat" *> "$out\sign-catalog.log";if($LASTEXITCODE){throw 'Catalog signing failed'}
$manifest=@(Get-ChildItem $out -File|Where-Object Extension -in '.sys','.dll','.cat','.inf','.cer'|ForEach-Object {[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}})
$manifest|ConvertTo-Json -Depth 4|Set-Content "$out\sha256.json"
[ordered]@{prepared=$true;installed=$false;hardware_id='ROOT\NPURENDER_NATIVE_PROBE';service='nr_native_probe';version='0.0.1333.0';signer_thumbprint=$thumb;unsigned_umd_sha256=$build.binary_sha256.'nr_native_probe_umd.dll';umd_build='native-tree-v32';frame_batch_umd=$true;files=$manifest;admission_protocol=3;registered_execution_of_this_build=$false;kernel_binary_unchanged=$true;desktop_offloaded=$false;finished=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
Get-Content "$out\report.json"
