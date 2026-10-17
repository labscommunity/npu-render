$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\physical-gpu-idd-v2-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;driver_installed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='waiting-for-packet-desktop';builds=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(75)
 while($true){$q=Get-Content "$vm\ptl-combined-desktop-v8\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Desktop completion deadline'};Start-Sleep -Seconds 3}
 $deadline=[DateTime]::UtcNow.AddMinutes(10)
 while($true){$q=Get-Content "$vm\physical-gpu-choice-v2-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'GPU control deadline'};Start-Sleep -Seconds 3}
 if(!$q.passed){throw 'Physical GPU control not qualified'}
 $q=Get-Content "$vm\physical-gpu-idd-v1-qualification\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Prior control build still active'}
 $desktop=if(Test-Path "$vm\native-desktop-admission-v66\report.json"){'native-desktop-admission-v66'}elseif(Test-Path "$vm\native-desktop-admission-v65\report.json"){'native-desktop-admission-v65'}else{'native-desktop-admission-v64'}
 $q=Get-Content "$vm\$desktop\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $report.active_phase='build-four-control-configurations';Save-Report
 $child=Start-Process powershell.exe -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\build-physical-gpu-idd-v1.ps1"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\build.stdout.log" -RedirectStandardError "$out\build.stderr.log"
 try{if(!$child.WaitForExit(900000)){$child.Kill();$child.WaitForExit();throw 'Build deadline'};$child.Refresh();if($child.ExitCode){throw 'Control build failed'}}finally{$child.Dispose()}
 foreach($kind in @('native-gpu','guest-gpu','native-paired','guest-paired')){
  $q=Get-Content "$r\builds\physical-gpu-idd-v1-$kind\build-report.json" -Raw|ConvertFrom-Json
  if(!$q.clean_build -or $q.cpu_contract_suites -ne 5 -or $q.driver_installed){throw 'Control build proof incomplete'}
  $report.builds+=@{kind=$kind;passed=$true;binary_sha256=$q.binary_sha256}
 }
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
