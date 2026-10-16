$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-combined-desktop-v3"
if(Test-Path $out){throw 'Combined desktop coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-full-worker';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
function Controller([string]$file,[string]$name,[string[]]$extra=@()){
 $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\'+$file+'"'))+$extra
 $child=Start-Process powershell.exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.log" -RedirectStandardError "$out\$name.stderr.log"
 try{if($child.ExitCode){throw "$name controller failed with exit $($child.ExitCode)"}}finally{$child.Dispose()}
}
function Stop-Preflight{
 if(Test-Path "$vm\native-topology-v87"){New-Item -ItemType File "$vm\native-topology-v87\stop" -Force|Out-Null}
 $preflight=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V87 -ErrorAction SilentlyContinue
 if($preflight){if($preflight.Actions.Arguments -notlike '*native-topology-v87*'){throw 'Preflight task identity differs'};$until=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $preflight.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $preflight.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $preflight.TaskName -Confirm:$false}
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-worker-v15-qualification-v2\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Full worker deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Full worker qualification incomplete'}
 $q=Get-Content "$vm\ptl-native-worker-v15-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.all_prior_worker_v14_outputs_preserved -or !$q.program_tickets_matched -or !$q.geometry_tickets_matched -or !$q.complex_tickets_matched -or !$q.rectangle_tickets_matched -or $q.worker_sha256 -ne 'f055699f18c06d4ee437859741c453ba6bbfe5e17646b67646e52db84503dc9a'){throw 'Worker proof incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $prior=Get-Content "$vm\native-registration-v39\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'Previous registration cleanup incomplete'}
 $report.active_phase='pre-registration-readiness';Save-Report
 try{Controller 'start-native-topology-v87.ps1' 'preflight'}catch{
  $state=Get-Content "$vm\native-topology-v87\report.json" -Raw|ConvertFrom-Json
  if(!$state.completed -or $state.screen_saver_settings.secure -ne $false -or $state.after_screen_saver_close.input_name -ne 'Screen-saver'){throw}
  Controller 'dismiss-native-saver-v25.ps1' 'nonsecure-saver'
 }
 Controller 'start-native-topology-v89.ps1' 'readiness'
 Stop-Preflight
 $report.phases+=@{phase='pre-registration-readiness';passed=$true}
 $report.active_phase='registered-d3d';Save-Report
 Controller 'run-native-registration-v40.ps1' 'registration' @('-Mode','render')
 $q=Get-Content "$vm\native-registration-v40\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 Controller 'run-ptl-native-live-v15.ps1' 'live'
 $q=Get-Content "$vm\ptl-native-live-v15\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='timing-analysis';Save-Report
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-latency-v11.py" --root $vm --output "$vm\native-latency-v13-analysis" > "$out\analysis.stdout.log" 2> "$out\analysis.stderr.log";if($LASTEXITCODE){throw 'Causal analysis failed'}
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-presentation-v9.py" --root $vm --output "$vm\native-presentation-v9-analysis" > "$out\presentation.stdout.log" 2> "$out\presentation.stderr.log";if($LASTEXITCODE){throw 'Presentation analysis failed'}
 $report.phases+=@{phase='timing-analysis';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 Stop-Preflight
 if(Test-Path "$vm\native-topology-v89"){New-Item -ItemType File "$vm\native-topology-v89\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V89 -ErrorAction SilentlyContinue
 if($task){if($task.Actions.Arguments -notlike '*native-topology-v89*'){throw 'Readiness task identity differs'};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
