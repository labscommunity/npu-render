$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\physical-gpu-desktop-v1-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;hardware_gpu_control=$true;npu_desktop_offloaded=$false;tahoma_tested=$false;active_phase='waiting-for-control-build'}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Controller([string]$file,[string]$label,[string[]]$extra=@(),[int]$seconds=600){
 $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\'+$file+'"'))+$extra
 $child=Start-Process powershell.exe -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput "$out\$label.stdout.log" -RedirectStandardError "$out\$label.stderr.log"
 try{if(!$child.WaitForExit($seconds*1000)){$child.Kill();$child.WaitForExit();throw "$label deadline"};$child.Refresh();if($child.ExitCode){throw "$label failed"}}finally{$child.Dispose()}
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(70)
 while($true){$q=Get-Content "$vm\physical-gpu-idd-v1-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Control build deadline'};Start-Sleep -Seconds 3}
 if(!$q.passed -or @($q.builds).Count -ne 4){throw 'Control build qualification incomplete'}
 $q=Get-Content "$vm\ptl-combined-desktop-v8\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Packet desktop qualification incomplete'}
 $q=Get-Content "$vm\native-desktop-admission-v66\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $report.active_phase='prepare-local-control-package';Save-Report
 Controller 'package-physical-gpu-idd-v1.ps1' 'package'
 $report.active_phase='ninety-second-hardware-routing';Save-Report
 Controller 'run-physical-gpu-desktop-v1.ps1' 'routing' @('-Mode','admit')
 $q=Get-Content "$vm\physical-gpu-desktop-v1\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after -or !$q.actual_hardware_gpu_route -or $q.actual_dwm_npu_execution){throw 'Hardware routing evidence incomplete'}
 $report.passed=$true;$report.cleanup_passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 if(!$report.passed -and (Test-Path "$vm\physical-gpu-desktop-v1\ownership.json")){
  try{
   $clean=$false;try{$q=Get-Content "$vm\physical-gpu-desktop-v1\report.json" -Raw|ConvertFrom-Json;$clean=$q.cleanup_passed}catch{}
   if(!$clean){Controller 'recover-physical-gpu-desktop-v1.ps1' 'outer-recovery' @('-Root',('"'+$vm+'\physical-gpu-desktop-v1"')) 300}
   $q=Get-Content "$vm\physical-gpu-desktop-v1\recovery-report.json" -Raw|ConvertFrom-Json
   $report.cleanup_passed=[bool]$q.passed
  }catch{$report.cleanup_passed=$false;$report.cleanup_error=$_.Exception.Message}
 }
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
