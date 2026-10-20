$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-combined-desktop-v9"
if(Test-Path $out){throw 'Combined desktop coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-full-worker';phases=@()}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\atomic_lab_report_v1.ps1")))
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Controller([string]$file,[string]$name,[string[]]$extra=@()){
 $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\'+$file+'"'))+$extra
 $child=Start-Process powershell.exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.log" -RedirectStandardError "$out\$name.stderr.log"
 try{if($child.ExitCode){throw "$name controller failed with exit $($child.ExitCode)"}}finally{$child.Dispose()}
}
function Stop-Preflight{
 if(Test-Path "$vm\native-topology-v109"){New-Item -ItemType File "$vm\native-topology-v109\stop" -Force|Out-Null}
 $preflight=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V109 -ErrorAction SilentlyContinue
 if($preflight){if($preflight.Actions.Arguments -notlike '*native-topology-v109*'){throw 'Preflight task identity differs'};$until=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $preflight.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $preflight.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $preflight.TaskName -Confirm:$false}
}
try{
 $report.active_phase='waiting-for-host-tensor-comparison';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(120)
 while($true){$q=Get-Content "$vm\ptl-worker-v20-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Host tensor comparison deadline'};Start-Sleep -Seconds 3}
 if(!$q.passed){throw 'Host tensor qualification incomplete'}
 $q=Get-Content "$vm\ptl-native-worker-v20-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.passed -or !$q.all_worker_v19_outputs_preserved -or $q.worker_sha256 -ne '92028cab6f47a1293e39e9e0ebe05bc1d0529e54ea57b4f6a5bf51274ff744e0'){throw 'Host tensor image proof incomplete'}
 $comparison=Get-Content "$vm\host-tensor-comparison-v1\report.json" -Raw|ConvertFrom-Json
 if(!$comparison.completed -or !$comparison.passed -or $comparison.summary.pairs -ne 1944 -or $comparison.summary.new_parent_ms -ge .95*$comparison.summary.old_parent_ms){throw 'Host tensors lack five percent aggregate paired improvement'}
 $report.paired_host_tensor=$comparison.summary
 $report.active_phase='waiting-for-host-tensor-recovery';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(20)
 while($true){$q=Get-Content "$vm\host-tensor-provider-recovery-v1-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Host tensor recovery deadline'};Start-Sleep -Seconds 3}
 if(!$q.passed){throw 'Host tensor recovery not qualified'}
 $q=Get-Content "$vm\host-tensor-provider-recovery-v1\report.json" -Raw|ConvertFrom-Json
 if(!$q.passed -or @($q.phases).Count -ne 2 -or $q.worker_sha256 -ne '92028cab6f47a1293e39e9e0ebe05bc1d0529e54ea57b4f6a5bf51274ff744e0'){throw 'Host tensor recovery proof differs'}
 $prior=Get-Content "$vm\physical-gpu-desktop-v2\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'GPU baseline not restored'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $prior=Get-Content "$vm\native-registration-v45\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'Previous registration cleanup incomplete'}
 $report.active_phase='pre-registration-readiness';Save-Report
 try{Controller 'start-native-topology-v109.ps1' 'preflight'}catch{
  $state=Get-Content "$vm\native-topology-v109\report.json" -Raw|ConvertFrom-Json
  if(!$state.completed -or $state.screen_saver_settings.secure -ne $false -or $state.after_screen_saver_close.input_name -ne 'Screen-saver'){throw}
  Controller 'dismiss-native-saver-v32.ps1' 'nonsecure-saver'
 }
 Controller 'start-native-topology-v110.ps1' 'readiness'
 Stop-Preflight
 $report.phases+=@{phase='pre-registration-readiness';passed=$true}
 $report.active_phase='registered-d3d';Save-Report
 Controller 'run-native-registration-v46.ps1' 'registration' @('-Mode','render')
 $q=Get-Content "$vm\native-registration-v46\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 Controller 'run-ptl-native-live-v21.ps1' 'live'
 $q=Get-Content "$vm\ptl-native-live-v21\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='timing-analysis';Save-Report
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-native-latency-v19.py"'),'--root',('"'+$vm+'"'),'--output',('"'+$vm+'\native-latency-v21-analysis"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\analysis.stdout.log" -RedirectStandardError "$out\analysis.stderr.log"
 try{if($analysis.ExitCode){throw 'analysis failed; full stderr retained'}}finally{$analysis.Dispose()}
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-native-presentation-v18.py"'),'--root',('"'+$vm+'"'),'--output',('"'+$vm+'\native-presentation-v18-analysis"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\presentation.stdout.log" -RedirectStandardError "$out\presentation.stderr.log"
 try{if($analysis.ExitCode){throw 'presentation failed; full stderr retained'}}finally{$analysis.Dispose()}
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-repeated-input-v2.py"'),'--trial',('"'+$vm+'\native-live-v57"'),'--output',('"'+$vm+'\native-repeated-input-v4-analysis"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\repeat-analysis.stdout.log" -RedirectStandardError "$out\repeat-analysis.stderr.log"
 try{if(!$analysis.WaitForExit(60000)){$analysis.Kill();$analysis.WaitForExit();throw 'Repeated analysis deadline'};$analysis.Refresh();if($analysis.ExitCode){throw 'Repeated input analysis failed'}}finally{$analysis.Dispose()}
 $report.phases+=@{phase='timing-analysis';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 Stop-Preflight
 if(Test-Path "$vm\native-topology-v110"){New-Item -ItemType File "$vm\native-topology-v110\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V110 -ErrorAction SilentlyContinue
 if($task){if($task.Actions.Arguments -notlike '*native-topology-v110*'){throw 'Readiness task identity differs'};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
