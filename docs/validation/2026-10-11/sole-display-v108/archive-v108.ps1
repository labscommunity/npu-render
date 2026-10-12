$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try {
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop'
  if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Wrong guest'}
  foreach($name in 'lab-v108-render-remove','lab-v9ap-readback-remove'){
   $report=Get-Content "C:\npu-lab\$name.json" -Raw|ConvertFrom-Json
   if(!$report.completed -or !$report.passed){
    if($name -ne 'lab-v9ap-readback-remove'){throw 'Cleanup incomplete'}
    $fixed=Get-Content C:\npu-lab\lab-v108-recovery-verification.json -Raw|ConvertFrom-Json
    if(!$fixed.passed -or $fixed.custom_devices -ne 0 -or !$fixed.owned_indirect_inf_absent){throw 'Recovery incomplete'}
   }
  }
  $out='C:\npu-lab\v108-after-cleanup'
  if(Test-Path $out){throw 'Archive exists'}
  New-Item -ItemType Directory $out|Out-Null
  Copy-Item C:\npu-lab\umd-trace,C:\npu-lab\idd-trace $out -Recurse
  Get-ChildItem C:\npu-lab -File|Where-Object {($_.Name -like 'lab-v108-*' -or $_.Name -like 'lab-v9ap-*' -or $_.Name -like 'idd-topology-v9ap*' -or $_.Name -eq 'warm-indexed-v108.json') -and $_.Extension -ne '.etl'}|Copy-Item -Destination $out
  Get-ChildItem $out -File -Recurse|ForEach-Object {[ordered]@{name=$_.FullName.Substring($out.Length+1);bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json|Set-Content C:\npu-lab\v108-after-cleanup-sha256.json
  Compress-Archive -Path "$out\*" -DestinationPath C:\npu-lab\v108-after-cleanup.zip
 }
 foreach($name in 'v108-after-cleanup.zip','v108-after-cleanup-sha256.json','lab-v108-render-observation.etl'){
  Copy-Item "C:\npu-lab\$name" "$r\vm\$name" -FromSession $nrLab
 }
 'v108 archive preserved'
} finally {Remove-PSSession $nrLab}
