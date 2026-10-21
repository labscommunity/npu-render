$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-native-live-v37"
if(Test-Path $out){throw 'Scoped native trial exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;phases=@()}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\atomic_lab_report_v1.ps1")))
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Stop-Readiness{
 if(Test-Path "$vm\native-topology-v150"){New-Item -ItemType File "$vm\native-topology-v150\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V150 -ErrorAction SilentlyContinue
 if($task){$deadline=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $task.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
}
try{
 foreach($name in @('native-frame-publication-v2-qualification','native-cursor-guard-v5-qualification')){$q=Get-Content "$vm\$name\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw "Delivery helper not qualified: $name"}}
 $q=Get-Content "$vm\native-desktop-admission-v74\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed){throw 'Native cleanup incomplete'}
 if(@(Get-Process nr_registered_local_indexed_worker,nr_pixel_merger_fusion,nr_uniform_draw_diagnostic,nr_native_topology,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $report.active_phase='interactive-readiness';Save-Report
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V150 -ErrorAction Stop
 if($task.State -ne 'Running' -or !(Test-Path "$vm\native-topology-v150\samples.jsonl")){throw 'Pre-registration readiness is not active'}
 $last=Get-Content "$vm\native-topology-v150\samples.jsonl" -Tail 1|ConvertFrom-Json
 if($last.exit_code -ne 0 -or ([DateTime]::UtcNow-([DateTimeOffset]::Parse($last.observed)).UtcDateTime).TotalSeconds -gt 15){throw 'Pre-registration readiness sample stale'}
 # Keep the readiness display-awake request alive until the live controller exits.
 $report.active_phase='native-live-v73';Save-Report
 $child=Start-Process powershell.exe -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\run-native-live-v73.ps1"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\live.stdout.log" -RedirectStandardError "$out\live.stderr.log"
 try{$report.live_exit=$child.ExitCode;if($child.ExitCode){throw 'Scoped native integration failed'}}finally{$child.Dispose()}
 $q=Get-Content "$vm\native-live-v73\report.json" -Raw|ConvertFrom-Json
 if(!$q.compiled_turbo_import_audit_passed -or !$q.turbo_import_audit_passed -or !$q.provider_generation_audit_passed -or $q.frame_batches -lt 1 -or !$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.shared_default_application_qualified -or !$q.repeated_input_execution_passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='native-live-v73';passed=$true;cleanup_passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{Stop-Readiness;$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
