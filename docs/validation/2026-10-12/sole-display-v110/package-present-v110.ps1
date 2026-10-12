param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$SourceRevision)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$root="$env:LOCALAPPDATA\npu-render";$previous="$root\vm\driver-package-v47-constant-updates";$package="$root\vm\driver-package-v110-compositor"
if(Test-Path $package){throw 'Refusing to overwrite package'}
foreach($file in (Get-Content "$previous\sha256.json" -Raw | ConvertFrom-Json)){
 if((Get-FileHash (Join-Path $previous $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Prior package mismatch'}
}
$qualified=Get-Content "$root\vm\performance-v216\report.json" -Raw|ConvertFrom-Json
if(!$qualified.all_checks_passed -or $qualified.build -ne 'present-umd-v216'){throw 'Full runtime qualification is required'}
$candidate=Get-Content "$root\builds\present-umd-v216\registered-build-report.json" -Raw|ConvertFrom-Json
$legacy=Get-Content "$root\builds\present-umd-v216-legacy\registered-build-report.json" -Raw|ConvertFrom-Json
foreach($item in $candidate.source_sha256.psobject.Properties){if($legacy.source_sha256.($item.Name) -ne $item.Value){throw 'Host and guest sources differ'}}
$build="$root\builds\allocation-capacity-v3"
$report=Get-Content "$build\build-report.json" -Raw | ConvertFrom-Json
if(!$report.clean_build -or !$report.lab_aperture_adapter_enabled -or !$report.start_enabled -or !$report.lab_paging_adapter_enabled){throw 'Expected isolated system-page adapter'}
$source='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25'
foreach($item in $report.source_sha256.psobject.Properties){if((Get-FileHash (Join-Path $source $item.Name) -Algorithm SHA256).Hash -ne $item.Value){throw 'Source mismatch'}}
if((Get-FileHash "$build\Release\nr_render_kmd.sys" -Algorithm SHA256).Hash -ne $report.sha256 -or (Get-FileHash "$build\Release\nr_render_kmd.pdb" -Algorithm SHA256).Hash -ne $report.pdb_sha256){throw 'Binary or symbols mismatch'}
New-Item -ItemType Directory $package | Out-Null
Copy-Item "$build\Release\nr_render_kmd.sys" $package
Copy-Item "$build\build-report.json" $package
$umd="$root\builds\present-umd-v216-legacy"
$umdReport=Get-Content "$umd\registered-build-report.json" -Raw | ConvertFrom-Json
if(!$umdReport.clean_build){throw 'Clean UMD build required'}
foreach($item in $umdReport.source_sha256.psobject.Properties){if((Get-FileHash (Join-Path $source $item.Name) -Algorithm SHA256).Hash -ne $item.Value){throw 'UMD source mismatch'}}
foreach($item in $umdReport.binary_sha256.psobject.Properties){if((Get-FileHash (Join-Path "$umd\Release" $item.Name) -Algorithm SHA256).Hash -ne $item.Value){throw 'UMD binary mismatch'}}
Copy-Item "$umd\Release\nr_fill_umd.dll" $package
Copy-Item "$umd\registered-build-report.json" $package
Copy-Item "$root\vm\v109-machine-test-signing.cer" "$package\test-signing.cer"
[ordered]@{source_revision=$SourceRevision;kind='isolated-dxgi-presentation-adapter';umd_source_revision=$SourceRevision;start_enabled=$true;aperture_advertised=$true;windows_allocations_tested=$false;npu_execution_tested=$false;desktop_offload_tested=$false} | ConvertTo-Json | Set-Content "$package\provenance.json"
$inf=(Get-Content "$previous\nr_render_lab.inf" -Raw).Replace('DriverVer=10/10/2026,0.0.36.0','DriverVer=10/12/2026,0.0.98.0').Replace('NPU Render Direct3D Lab (VM only; restricted renderer)','NPU Render System Page Lab (VM only; restricted renderer)')
$runtime="$root\vm\registered-runtime-v31"
$runtimeFiles=Get-Content "$runtime\sha256.json" -Raw | ConvertFrom-Json
if($runtimeFiles.Count -ne 16){throw 'Expected verified runtime closure'}
foreach($file in $runtimeFiles){
 if($file.name -notmatch '^[a-zA-Z0-9_.-]+\.dll$' -or (Get-FileHash (Join-Path $runtime $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Runtime dependency mismatch'}
 Copy-Item (Join-Path $runtime $file.name) $package
}
Copy-Item "$runtime\sha256.json" "$package\runtime-sha256.json"
Copy-Item "$runtime\provenance.json" "$package\runtime-provenance.json"
Set-Content "$package\nr_render_lab.inf" $inf -Encoding ascii
& "$root\dependencies\wdk-tools\infverif.exe" /w "$package\nr_render_lab.inf" *> "$package\infverif.log"
if($LASTEXITCODE -ne 0){throw 'INF invalid'}
$sign='C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
& $sign sign /sm /s My /fd SHA256 /sha1 3112355AF6B6C6265E9B849662D3E02F6797343A "$package\nr_render_kmd.sys" *> "$package\sign-sys.log"
if($LASTEXITCODE -ne 0){throw 'Kernel signing failed'}
& $sign sign /sm /s My /fd SHA256 /sha1 3112355AF6B6C6265E9B849662D3E02F6797343A "$package\nr_fill_umd.dll" *> "$package\sign-umd.log"
if($LASTEXITCODE -ne 0){throw 'UMD signing failed'}
& "$root\dependencies\wdk-tools\Inf2Cat.exe" "/driver:$package" /os:10_GE_X64 *> "$package\catalog.log"
if($LASTEXITCODE -ne 0){throw 'Catalog generation failed'}
& $sign sign /sm /s My /fd SHA256 /sha1 3112355AF6B6C6265E9B849662D3E02F6797343A "$package\nr_render_lab.cat" *> "$package\sign-catalog.log"
if($LASTEXITCODE -ne 0){throw 'Catalog signing failed'}
Get-ChildItem $package -File | Where-Object Extension -in '.sys','.dll','.cat','.inf','.cer','.json' | ForEach-Object {[ordered]@{name=$_.Name;sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}} | ConvertTo-Json | Set-Content "$package\sha256.json"
Get-Content "$package\sha256.json"
