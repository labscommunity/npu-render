$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try {
 $build=Get-Content "$r\builds\topology-duration-v33\build-report.json" -Raw|ConvertFrom-Json
 if(!$build.clean_build -or !$build.powershell_parser_passed){throw 'Topology build missing'}
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop'
  . ([scriptblock]::Create([IO.File]::ReadAllText('C:\npu-lab\lab_device_identity.ps1')));Assert-NrLabGuest
  foreach($name in 'lab-v119-render-remove','lab-v9ba-readback-remove'){$a=Get-Content "C:\npu-lab\$name.json" -Raw|ConvertFrom-Json;if(!$a.completed -or !$a.passed){throw 'Previous cleanup incomplete'}}
  foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){if(@(Get-NrLabDevice -HardwareId $id).Count){throw 'Custom device present'}}
  if(!(Test-Path C:\npu-lab\v119-after-cleanup.zip)){throw 'Archive missing'}
  foreach($package in 'driver-package-v119-compositor','driver-package-idd-readback-v9'){
   foreach($f in (Get-Content "C:\npu-lab\$package\sha256.json" -Raw|ConvertFrom-Json)){if((Get-FileHash "C:\npu-lab\$package\$($f.name)").Hash -ne $f.sha256){throw 'Package mismatch'}}
  }
  if(Test-Path C:\npu-lab\driver-package-v120-compositor){throw 'New package already exists'}
  Copy-Item C:\npu-lab\driver-package-v119-compositor C:\npu-lab\driver-package-v120-compositor -Recurse
  Move-Item C:\npu-lab\idd-trace C:\npu-lab\idd-trace-v9ba-preserved
  Move-Item C:\npu-lab\umd-trace C:\npu-lab\umd-trace-v119-preserved
  New-Item -ItemType Directory C:\npu-lab\umd-trace|Out-Null
  & icacls.exe C:\npu-lab\umd-trace /grant '*S-1-5-11:(OI)(CI)M' '*S-1-5-84-0-0-0-0-0:(OI)(CI)M' *> C:\npu-lab\umd-trace-v120-acl.log
  if($LASTEXITCODE){throw 'Trace ACL failed'}
 }
 foreach($n in 'test-signing.cer','nr_render_kmd.sys','nr_fill_umd.dll','nr_render_lab.inf','nr_render_lab.cat','build-report.json','registered-build-report.json','provenance.json','sha256.json'){
  Copy-Item "$r\vm\driver-package-v120-compositor\$n" "C:\npu-lab\driver-package-v120-compositor\$n" -ToSession $nrLab
 }
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop'
  foreach($f in (Get-Content C:\npu-lab\driver-package-v120-compositor\sha256.json -Raw|ConvertFrom-Json)){
   if((Get-FileHash "C:\npu-lab\driver-package-v120-compositor\$($f.name)").Hash -ne $f.sha256){throw 'New package transfer mismatch'}
  }
 }
 Copy-Item "$r\builds\topology-duration-v33\Release\nr_indirect_topology_gui.exe" C:\npu-lab\nr_indirect_topology_gui_v33.exe -ToSession $nrLab
 if((Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash C:\npu-lab\nr_indirect_topology_gui_v33.exe).Hash}) -ne $build.binary_sha256){throw 'Topology binary mismatch'}
 foreach($name in 'start_indirect_topology_task-v33.ps1','observe_registered_desktop-v33.ps1'){
  $source="$r\vm\topology-duration-v33\$name";Copy-Item $source "C:\npu-lab\$name" -ToSession $nrLab
  if((Get-FileHash $source).Hash -ne (Invoke-Command -Session $nrLab -ArgumentList $name -ScriptBlock {param($n)(Get-FileHash "C:\npu-lab\$n").Hash})){throw 'Harness mismatch'}
 }
 $componentRoot="$r\vm\component-coverage-v229\packets"
 $componentReport=Get-Content "$componentRoot\report.json" -Raw|ConvertFrom-Json
 if(!$componentReport.all_pixels_exact -or $componentReport.execution_device -ne 'NPU'){throw 'Component packets not qualified'}
 Invoke-Command -Session $nrLab -ScriptBlock {New-Item -ItemType Directory C:\npu-lab\component-packets-v229 -ErrorAction Stop|Out-Null}
 Copy-Item "$componentRoot\prepared.json" C:\npu-lab\component-packets-v229\prepared.json -ToSession $nrLab
 foreach($case in $componentReport.cases){
  if((Get-FileHash "$componentRoot\$($case.packet)").Hash.ToLowerInvariant() -ne $case.sha256){throw 'Component packet mismatch'}
  Copy-Item "$componentRoot\$($case.packet)" "C:\npu-lab\component-packets-v229\$($case.packet)" -ToSession $nrLab
 }
 foreach($name in 'warm-indexed-v120.ps1'){
  Copy-Item "$r\vm\$name" "C:\npu-lab\$name" -ToSession $nrLab
  if((Get-FileHash "$r\vm\$name").Hash -ne (Invoke-Command -Session $nrLab -ArgumentList $name -ScriptBlock {param($n)(Get-FileHash "C:\npu-lab\$n").Hash})){throw 'Warmup mismatch'}
 }
 $priorParser=Get-Content "$r\vm\topology-duration-v33-parser.json" -Raw|ConvertFrom-Json
 if(@($priorParser).Count -ne 8 -or @($priorParser|Where-Object {!$_.passed}).Count){throw 'Existing v33 parser qualification missing'}
 [ordered]@{trial='v120';staged=$true;package='v120';package_version='0.0.105.0';topology_binary_sha256=$build.binary_sha256;parser_cases=$priorParser;installed=$false}|ConvertTo-Json -Depth 6|Set-Content "$r\vm\v120-staging.json"
 'v120 staged; package and existing v33 parser qualification verified'
}finally{Remove-PSSession $nrLab}
