$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-combined-desktop-v7-v2"
if(Test-Path $out){throw 'Combined desktop coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-full-worker';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
function Controller([string]$file,[string]$name,[string[]]$extra=@()){
 $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\'+$file+'"'))+$extra
 $child=Start-Process powershell.exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.log" -RedirectStandardError "$out\$name.stderr.log"
 try{if($child.ExitCode){throw "$name controller failed with exit $($child.ExitCode)"}}finally{$child.Dispose()}
}
function Stop-Preflight{
 if(Test-Path "$vm\native-topology-v100"){New-Item -ItemType File "$vm\native-topology-v100\stop" -Force|Out-Null}
 $preflight=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V100 -ErrorAction SilentlyContinue
 if($preflight){if($preflight.Actions.Arguments -notlike '*native-topology-v100*'){throw 'Preflight task identity differs'};$until=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $preflight.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $preflight.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $preflight.TaskName -Confirm:$false}
}
try{
 $report.active_phase='waiting-for-transport-comparison';Save-Report
 $finish=[DateTime]::UtcNow.AddMinutes(60)
 while($true){$before=Get-Content "$vm\packet-transport-comparison-v1-qualification\report.json" -Raw|ConvertFrom-Json;if($before.completed){break};if([DateTime]::UtcNow -gt $finish){throw 'Transport comparison deadline'};Start-Sleep -Seconds 3}
 if(Test-Path "$vm\native-live-v55"){throw 'Repeated live trial already exists'}
 $report.active_phase='waiting-for-isolated-provider-tests';Save-Report
 $finish=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$previous=Get-Content "$vm\native-input-tools-v1-qualification\report.json" -Raw|ConvertFrom-Json;if($previous.completed){break};if([DateTime]::UtcNow -gt $finish){throw 'Isolated tests completion deadline'};Start-Sleep -Seconds 3}
 if(!$previous.passed){throw 'Input tools are not qualified'}
 $priorLive=Get-Content "$vm\ptl-combined-desktop-v6-completion-v2\report.json" -Raw|ConvertFrom-Json
 if(!$priorLive.completed -or !$priorLive.passed){throw 'Prior compact live trial not qualified'}
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-worker-v18-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Full worker deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Full worker qualification incomplete'}
 $q=Get-Content "$vm\ptl-native-worker-v18-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.all_prior_worker_v15_outputs_preserved -or !$q.program_tickets_matched -or !$q.geometry_tickets_matched -or !$q.complex_tickets_matched -or !$q.rectangle_tickets_matched -or $q.worker_sha256 -ne 'd75e2fb82ab038f02054931c9cc2e862c08546e2dfe363a23dd9997394b239a6'){throw 'Worker proof incomplete'}
 $report.active_phase='waiting-for-paired-program-comparison';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-program-compact-comparison-v4-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Paired comparison deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Paired program comparison incomplete'}
 $pairs=Get-Content "$vm\ptl-program-compact-comparison-v4-full\report.json" -Raw|ConvertFrom-Json
 if(!$pairs.passed -or @($pairs.cases).Count -ne 463 -or @($pairs.cases|Where-Object {!$_.passed}).Count){throw 'Complete image qualification missing'}
 $reference=@($pairs.cases|ForEach-Object {$_.reference_compatibility})
 if($reference.Count -ne 1852 -or @($reference|Where-Object {!$_.same_difference_positions_and_values}).Count){throw 'Reference discrepancy changed'}
 $report.reference_discrepant_variants=@($reference|Where-Object {!$_.reference_exact}).Count
 $report.image_qualification='exact preceding NPU outputs; any CPU-reference disagreement retained unchanged'
 $warm=@($pairs.cases|ForEach-Object {$_.pairs|Where-Object iteration -gt 0})
 if($warm.Count -ne 1389 -or @($warm|Where-Object {!$_.exact}).Count){throw 'Warm paired evidence incomplete'}
 $old=($warm|Measure-Object baseline_full_ms -Sum).Sum;$new=($warm|Measure-Object candidate_full_ms -Sum).Sum
 $report.warm_program_previous_ms=$old;$report.warm_program_compact_ms=$new
 if($new -ge .95*$old){throw 'Compact batches lack five percent paired improvement'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $prior=Get-Content "$vm\native-registration-v43\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'Previous registration cleanup incomplete'}
 $report.active_phase='pre-registration-readiness';Save-Report
 try{Controller 'start-native-topology-v100.ps1' 'preflight'}catch{
  $state=Get-Content "$vm\native-topology-v100\report.json" -Raw|ConvertFrom-Json
  if(!$state.completed -or $state.screen_saver_settings.secure -ne $false -or $state.after_screen_saver_close.input_name -ne 'Screen-saver'){throw}
  Controller 'dismiss-native-saver-v29.ps1' 'nonsecure-saver'
 }
 Controller 'start-native-topology-v101.ps1' 'readiness'
 Stop-Preflight
 $report.phases+=@{phase='pre-registration-readiness';passed=$true}
 $report.active_phase='registered-d3d';Save-Report
 Controller 'run-native-registration-v44.ps1' 'registration' @('-Mode','render')
 $q=Get-Content "$vm\native-registration-v44\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 Controller 'run-ptl-native-live-v19-v2.ps1' 'live'
 $q=Get-Content "$vm\ptl-native-live-v19\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='timing-analysis';Save-Report
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-native-latency-v17.py"'),'--root',('"'+$vm+'"'),'--output',('"'+$vm+'\native-latency-v19-analysis"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\analysis.stdout.log" -RedirectStandardError "$out\analysis.stderr.log"
 try{if($analysis.ExitCode){throw 'analysis failed; full stderr retained'}}finally{$analysis.Dispose()}
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-native-presentation-v16.py"'),'--root',('"'+$vm+'"'),'--output',('"'+$vm+'\native-presentation-v16-analysis"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\presentation.stdout.log" -RedirectStandardError "$out\presentation.stderr.log"
 try{if($analysis.ExitCode){throw 'presentation failed; full stderr retained'}}finally{$analysis.Dispose()}
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-repeated-input-v2.py"'),'--trial',('"'+$vm+'\native-live-v55"'),'--output',('"'+$vm+'\native-repeated-input-v2-analysis"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\repeat-analysis.stdout.log" -RedirectStandardError "$out\repeat-analysis.stderr.log"
 try{if(!$analysis.WaitForExit(60000)){$analysis.Kill();$analysis.WaitForExit();throw 'Repeated analysis deadline'};$analysis.Refresh();if($analysis.ExitCode){throw 'Repeated input analysis failed'}}finally{$analysis.Dispose()}
 $report.phases+=@{phase='timing-analysis';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 Stop-Preflight
 if(Test-Path "$vm\native-topology-v101"){New-Item -ItemType File "$vm\native-topology-v101\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V101 -ErrorAction SilentlyContinue
 if($task){if($task.Actions.Arguments -notlike '*native-topology-v101*'){throw 'Readiness task identity differs'};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
