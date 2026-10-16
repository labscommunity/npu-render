$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-combined-desktop-v2"
if(Test-Path $out){throw 'Combined desktop coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-full-worker';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
function Controller([string]$file,[string]$name,[string[]]$extra=@()){
 $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\'+$file+'"'))+$extra
 $child=Start-Process powershell.exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.log" -RedirectStandardError "$out\$name.stderr.log"
 try{if($child.ExitCode){throw "$name controller failed with exit $($child.ExitCode)"}}finally{$child.Dispose()}
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-worker-v14-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Full worker deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Full worker qualification incomplete'}
 $q=Get-Content "$vm\ptl-native-worker-v14-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.all_prior_worker_v13_outputs_preserved -or !$q.program_tickets_matched -or !$q.geometry_tickets_matched -or !$q.complex_tickets_matched -or !$q.rectangle_tickets_matched -or $q.worker_sha256 -ne 'c3f1501128673998939248f0663f2f16e7450e10d7e4f1d69aacc47799a04a47'){throw 'Worker proof incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $prior=Get-Content "$vm\native-registration-v38\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'Previous registration cleanup incomplete'}
 $report.active_phase='pre-registration-readiness';Save-Report
 Controller 'start-native-topology-v85.ps1' 'readiness'
 $report.phases+=@{phase='pre-registration-readiness';passed=$true}
 $report.active_phase='registered-d3d';Save-Report
 Controller 'run-native-registration-v39.ps1' 'registration' @('-Mode','render')
 $q=Get-Content "$vm\native-registration-v39\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 Controller 'run-ptl-native-live-v14.ps1' 'live'
 $q=Get-Content "$vm\ptl-native-live-v14\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='timing-analysis';Save-Report
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-latency-v10.py" --root $vm --output "$vm\native-latency-v12-analysis" > "$out\analysis.stdout.log" 2> "$out\analysis.stderr.log";if($LASTEXITCODE){throw 'Causal analysis failed'}
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-presentation-v7.py" --root $vm --output "$vm\native-presentation-v7-analysis" > "$out\presentation.stdout.log" 2> "$out\presentation.stderr.log";if($LASTEXITCODE){throw 'Presentation analysis failed'}
 $report.phases+=@{phase='timing-analysis';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 if(Test-Path "$vm\native-topology-v85"){New-Item -ItemType File "$vm\native-topology-v85\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V85 -ErrorAction SilentlyContinue
 if($task){if($task.Actions.Arguments -notlike '*native-topology-v85*'){throw 'Readiness task identity differs'};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
