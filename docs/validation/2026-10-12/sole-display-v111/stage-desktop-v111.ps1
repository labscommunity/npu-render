$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$nrRoot="$env:LOCALAPPDATA\npu-render"
Invoke-Command -Session $nrLab -ScriptBlock {
 $ErrorActionPreference='Stop'
 if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Wrong guest'}
 foreach($name in 'lab-v110-render-remove','lab-v9ar-readback-remove'){
  $removed=Get-Content "C:\npu-lab\$name.json" -Raw|ConvertFrom-Json
  if(!$removed.completed -or !$removed.passed){throw 'Prior cleanup incomplete'}
 }
 . ([scriptblock]::Create([IO.File]::ReadAllText('C:\npu-lab\lab_device_identity.ps1')))
 foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){if(@(Get-NrLabDevice -HardwareId $id).Count){throw 'Custom device still present'}}
 if(Test-Path C:\npu-lab\driver-package-v111-compositor){throw 'Package exists'}
 Copy-Item C:\npu-lab\driver-package-v110-compositor C:\npu-lab\driver-package-v111-compositor -Recurse
 Move-Item C:\npu-lab\idd-trace C:\npu-lab\idd-trace-v9ar-preserved
 Move-Item C:\npu-lab\umd-trace C:\npu-lab\umd-trace-v110-preserved
 New-Item -ItemType Directory C:\npu-lab\umd-trace|Out-Null
 & icacls.exe C:\npu-lab\umd-trace /grant '*S-1-5-11:(OI)(CI)M' '*S-1-5-84-0-0-0-0-0:(OI)(CI)M' *> C:\npu-lab\umd-trace-v111-acl.log
 if($LASTEXITCODE -ne 0){throw 'UMD trace ACL failed'}
 'Previous runtime closure copied'
}
foreach($n in 'test-signing.cer','nr_render_kmd.sys','nr_fill_umd.dll','nr_render_lab.inf','nr_render_lab.cat','build-report.json','registered-build-report.json','provenance.json','sha256.json'){
 Copy-Item "$nrRoot\vm\driver-package-v111-compositor\$n" "C:\npu-lab\driver-package-v111-compositor\$n" -ToSession $nrLab
 "Transferred $n"
}
Copy-Item "$nrRoot\builds\indirect-lab-v30\Release\nr_indirect_topology_gui.exe" C:\npu-lab\nr_indirect_topology_gui_v30.exe -ToSession $nrLab
Invoke-Command -Session $nrLab -ScriptBlock {
 $ErrorActionPreference='Stop'
 foreach($f in (Get-Content C:\npu-lab\driver-package-v111-compositor\sha256.json -Raw|ConvertFrom-Json)){
  if((Get-FileHash "C:\npu-lab\driver-package-v111-compositor\$($f.name)").Hash -ne $f.sha256){throw 'Package mismatch'}
 }
 $certificate='C:\npu-lab\driver-package-v111-compositor\test-signing.cer'
 if((Get-FileHash $certificate).Hash -ne 'CC86FE0DA269A229AE14D37E233E34C5F48158192DD87DBEEDB64DED7ADECA42'){throw 'Lab public certificate mismatch'}
 foreach($store in 'Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher'){
  $trusted=Import-Certificate -FilePath $certificate -CertStoreLocation $store
  if($trusted.Thumbprint -ne '3112355AF6B6C6265E9B849662D3E02F6797343A'){throw 'Unexpected lab signing identity'}
 }
 'v111 package and guest-only test trust verified'
}

Invoke-Command -Session $nrLab -ScriptBlock {
 $ErrorActionPreference='Stop'
 foreach($f in (Get-Content C:\npu-lab\driver-package-idd-readback-v9\sha256.json -Raw|ConvertFrom-Json)){
  if((Get-FileHash "C:\npu-lab\driver-package-idd-readback-v9\$($f.name)").Hash -ne $f.sha256){throw 'IDD package mismatch'}
 }
 'Existing v9 IDD package verified'
}

Copy-Item "$nrRoot\vm\start_indirect_topology_task.ps1" C:\npu-lab\start_indirect_topology_task.ps1 -ToSession $nrLab
Invoke-Command -Session $nrLab -ScriptBlock {
 $ErrorActionPreference='Stop'
 $path='C:\npu-lab\start_indirect_topology_task.ps1'
 if((Get-FileHash $path).Hash -ne '6f6bc8e9e983976fac4c83c7db08776983545cf3f1fb0cded32fb47247e57c6e'){throw 'Topology launcher mismatch'}
 'Sole-indirect launcher verified'
}

if((Get-FileHash "$nrRoot\vm\v73-modulated-point-warm-packet.bin").Hash -ne '4d192ed55a6f565fdc0c0ae9a2745a6917739c00a0841defa4d66977302c3b9d'){throw 'New warmup packet mismatch'}
Copy-Item "$nrRoot\vm\v73-modulated-point-warm-packet.bin" "C:\npu-lab\v73-modulated-point-warm-packet.bin" -ToSession $nrLab
Invoke-Command -Session $nrLab -ScriptBlock {
 $ErrorActionPreference='Stop'
 if((Get-FileHash "C:\npu-lab\v73-modulated-point-warm-packet.bin").Hash -ne '4d192ed55a6f565fdc0c0ae9a2745a6917739c00a0841defa4d66977302c3b9d'){throw 'Guest warmup packet mismatch'}
 'New point-modulated warmup packet verified'
}

foreach($name in 'lab_report.ps1','lab_device_identity.ps1','manage_lab_driver.ps1','observe_registered_desktop.ps1','warm-indexed-v111.ps1','v75-dwm-warm-packet.bin','v75-shell-warm-packet.bin','v76-texture-triangles-warm-packet.bin','v76-resource-warm-packet.bin','v77-dwm-81-warm-packet.bin','v77-dwm-24-warm-packet.bin'){
 Copy-Item "$nrRoot\vm\$name" "C:\npu-lab\$name" -ToSession $nrLab
 if((Get-FileHash "$nrRoot\vm\$name").Hash -ne (Invoke-Command -Session $nrLab -ArgumentList $name -ScriptBlock {param($n)(Get-FileHash "C:\npu-lab\$n").Hash})){throw 'Test harness transfer mismatch'}
}

if((Get-FileHash "$nrRoot\vm\v104-slow-1-warm-packet.bin").Hash -ne '8835556bc51a0b47d82021748a8d5981f1fd5fc2b5ca321f25ff5d2a34abb62e'){throw 'Captured prewarm transfer mismatch'}
Copy-Item "$nrRoot\vm\v104-slow-1-warm-packet.bin" "C:\npu-lab\v104-slow-1-warm-packet.bin" -ToSession $nrLab
if((Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash 'C:\npu-lab\v104-slow-1-warm-packet.bin').Hash}) -ne '8835556bc51a0b47d82021748a8d5981f1fd5fc2b5ca321f25ff5d2a34abb62e'){throw 'Guest captured prewarm mismatch'}

if((Get-FileHash "$nrRoot\vm\v104-slow-2-warm-packet.bin").Hash -ne 'c0faa20f4360ff63b8d8236902a7048b1d187796e65db2571be39cfb36cea98d'){throw 'Captured prewarm transfer mismatch'}
Copy-Item "$nrRoot\vm\v104-slow-2-warm-packet.bin" "C:\npu-lab\v104-slow-2-warm-packet.bin" -ToSession $nrLab
if((Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash 'C:\npu-lab\v104-slow-2-warm-packet.bin').Hash}) -ne 'c0faa20f4360ff63b8d8236902a7048b1d187796e65db2571be39cfb36cea98d'){throw 'Guest captured prewarm mismatch'}

if((Get-FileHash "$nrRoot\vm\v104-slow-3-warm-packet.bin").Hash -ne 'e028a02e45cf2b46b916c1fe4547fb3843c475f12b807ddba7984cbc1878bf00'){throw 'Captured prewarm transfer mismatch'}
Copy-Item "$nrRoot\vm\v104-slow-3-warm-packet.bin" "C:\npu-lab\v104-slow-3-warm-packet.bin" -ToSession $nrLab
if((Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash 'C:\npu-lab\v104-slow-3-warm-packet.bin').Hash}) -ne 'e028a02e45cf2b46b916c1fe4547fb3843c475f12b807ddba7984cbc1878bf00'){throw 'Guest captured prewarm mismatch'}

if((Get-FileHash "$nrRoot\vm\v104-slow-4-warm-packet.bin").Hash -ne '7115b978a107d6117fe5b809a1c94cab9a2549cfa59112c7dbcc72f4c5254521'){throw 'Captured prewarm transfer mismatch'}
Copy-Item "$nrRoot\vm\v104-slow-4-warm-packet.bin" "C:\npu-lab\v104-slow-4-warm-packet.bin" -ToSession $nrLab
if((Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash 'C:\npu-lab\v104-slow-4-warm-packet.bin').Hash}) -ne '7115b978a107d6117fe5b809a1c94cab9a2549cfa59112c7dbcc72f4c5254521'){throw 'Guest captured prewarm mismatch'}
