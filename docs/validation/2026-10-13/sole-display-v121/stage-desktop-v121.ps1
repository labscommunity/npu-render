$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$qualified=Get-Content "$r\vm\desktop-worker-v6-qualified.json" -Raw|ConvertFrom-Json;if(!$qualified.all_checks_passed){throw 'Host worker not qualified'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try{
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop';. ([scriptblock]::Create([IO.File]::ReadAllText('C:\npu-lab\lab_device_identity.ps1')));Assert-NrLabGuest
  foreach($name in 'lab-v120-render-remove','lab-v9bb-readback-remove'){$a=Get-Content "C:\npu-lab\$name.json" -Raw|ConvertFrom-Json;if(!$a.completed -or !$a.passed){throw 'Previous cleanup incomplete'}}
  foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){if(@(Get-NrLabDevice -HardwareId $id).Count){throw 'Custom device present'}}
  if(!(Test-Path C:\npu-lab\v120-after-cleanup.zip)){throw 'Prior archive missing'}
  foreach($package in 'driver-package-v120-compositor','driver-package-idd-readback-v9'){foreach($f in (Get-Content "C:\npu-lab\$package\sha256.json" -Raw|ConvertFrom-Json)){if((Get-FileHash "C:\npu-lab\$package\$($f.name)").Hash -ne $f.sha256){throw 'Package mismatch'}}}
  if((Get-FileHash C:\npu-lab\driver-package-v120-compositor\nr_fill_umd.dll).Hash -ne '6A2557DAE2E68B7B27E614FFE1540D9A553CCA18A348158E6BEEB06310BA6939'){throw 'Unexpected guest renderer'}
  if((Get-FileHash C:\npu-lab\nr_indirect_topology_gui_v33.exe).Hash -ne '54DD65AF5A3B40386DC2EAA543CCA8E9C23B362E228231E0E19DC2F9A6FC9175'){throw 'Unexpected topology helper'}
  foreach($path in 'idd-trace-v9bb-preserved','umd-trace-v120-preserved','warm-indexed-v121.ps1'){if(Test-Path "C:\npu-lab\$path"){throw 'New trial evidence already exists'}}
  Move-Item C:\npu-lab\idd-trace C:\npu-lab\idd-trace-v9bb-preserved;Move-Item C:\npu-lab\umd-trace C:\npu-lab\umd-trace-v120-preserved
  New-Item -ItemType Directory C:\npu-lab\umd-trace|Out-Null
  & icacls.exe C:\npu-lab\umd-trace /grant '*S-1-5-11:(OI)(CI)M' '*S-1-5-84-0-0-0-0-0:(OI)(CI)M' *> C:\npu-lab\umd-trace-v121-acl.log;if($LASTEXITCODE){throw 'Trace ACL failed'}
 }
 Copy-Item "$r\vm\warm-indexed-v121.ps1" C:\npu-lab\warm-indexed-v121.ps1 -ToSession $nrLab
 if((Get-FileHash "$r\vm\warm-indexed-v121.ps1").Hash -ne (Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash C:\npu-lab\warm-indexed-v121.ps1).Hash})){throw 'Warmup transfer mismatch'}
 [ordered]@{trial='v121';staged=$true;existing_package='v120';package_version='0.0.105.0';host_worker='desktop-worker-v6';guest_renderer_changed=$false;installed=$false}|ConvertTo-Json|Set-Content "$r\vm\v121-staging.json"
 'v121 staged with previously qualified signed guest package'
}finally{Remove-PSSession $nrLab}
