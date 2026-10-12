$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v114\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Stream cleanup incomplete'}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try{
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop';. ([scriptblock]::Create([IO.File]::ReadAllText('C:\npu-lab\lab_device_identity.ps1')));Assert-NrLabGuest
  $render=Get-Content C:\npu-lab\lab-v114-render-remove.json -Raw|ConvertFrom-Json
  $idd=Get-Content C:\npu-lab\lab-v9av-readback-remove.json -Raw|ConvertFrom-Json
  if(!$render.completed -or !$render.passed -or !$idd.completed -or $idd.passed){throw 'Unexpected cleanup state'}
  $saved=Get-Content C:\npu-lab\lab-v9av-readback-owned.json -Raw|ConvertFrom-Json
  if($saved.hardware_id -ne 'ROOT\NPURENDER_INDIRECT_LAB' -or $saved.version -ne '0.0.9.0' -or $saved.inf -ne 'oem3.inf'){throw 'Indirect identity mismatch'}
  if(Test-Path C:\npu-lab\indexed-pipe-active.txt){throw 'Stream still configured'}
  $out='C:\npu-lab\v114-before-restart';if(Test-Path $out){throw 'Restart evidence exists'};New-Item -ItemType Directory $out|Out-Null
  Copy-Item C:\npu-lab\umd-trace,C:\npu-lab\idd-trace $out -Recurse
  Get-ChildItem C:\npu-lab -File|Where-Object{($_.Name -like 'lab-v114-*' -or $_.Name -like 'lab-v9av-*' -or $_.Name -like 'idd-topology-v9av*' -or $_.Name -like 'v114-early-*' -or $_.Name -eq 'warm-indexed-v114.json') -and $_.Extension -ne '.etl'}|Copy-Item -Destination $out
  Get-ChildItem $out -File -Recurse|ForEach-Object{@{name=$_.FullName.Substring($out.Length+1);bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json|Set-Content C:\npu-lab\v114-before-restart-sha256.json
  [ordered]@{at=[DateTime]::UtcNow.ToString('o');guest=$env:COMPUTERNAME;scope='disposable Windows VM only';reason='Owned indirect device disable requested reboot and finite removal did not pass';ordinary_render_driver_removed=$true;original_cleanup_passed=$false;reboot_requested=$true;physical_machine_restarted=$false;saved_indirect=$saved}|ConvertTo-Json -Depth 5|Set-Content C:\npu-lab\lab-v114-recovery-stage.json
 }
 Copy-Item C:\npu-lab\lab-v114-recovery-stage.json "$r\vm\lab-v114-recovery-stage.json" -FromSession $nrLab
 Invoke-Command -Session $nrLab -ScriptBlock {
  if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Owned guest required'}
  & shutdown.exe /r /t 5 /c 'Complete disposable NPU Render graphics-lab cleanup'
  if($LASTEXITCODE){throw 'Guest restart request failed'}
 }
 'Owned guest restart scheduled; physical host unchanged'
}finally{Remove-PSSession $nrLab}
