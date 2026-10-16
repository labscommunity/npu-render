$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-native-live-v2"
if(Test-Path $out){throw 'Scoped native trial exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
function Stop-Readiness{
 if(Test-Path "$vm\native-topology-v61"){New-Item -ItemType File "$vm\native-topology-v61\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V61 -ErrorAction SilentlyContinue
 if($task){$deadline=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $task.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
}
try{
 $q=Get-Content "$vm\ptl-native-worker-v3-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed){throw 'Captured timeout replay not complete'}
 foreach($name in @('native-frame-publication-v2-qualification','native-cursor-guard-v5-qualification')){$q=Get-Content "$vm\$name\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw "Delivery helper not qualified: $name"}}
 $q=Get-Content "$vm\native-desktop-admission-v47\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed){throw 'Native cleanup incomplete'}
 if(@(Get-Process nr_registered_local_indexed_worker,nr_pixel_merger_fusion,nr_uniform_draw_diagnostic,nr_native_topology,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $report.active_phase='interactive-readiness';Save-Report
 $ErrorActionPreference='Continue'
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$vm\start-native-topology-v61.ps1" > "$out\observer61.stdout.log" 2> "$out\observer61.stderr.log";$observerExit=$LASTEXITCODE
 $ErrorActionPreference='Stop'
 if($observerExit){& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$vm\dismiss-native-saver-v11.ps1" > "$out\saver.stdout.log" 2> "$out\saver.stderr.log";if($LASTEXITCODE){throw 'Unlocked desktop readiness failed'}}
 Stop-Readiness
 $report.active_phase='native-live-v38';Save-Report
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$vm\run-native-live-v38.ps1" > "$out\live.stdout.log" 2> "$out\live.stderr.log"
 if($LASTEXITCODE){throw 'Scoped native integration failed'}
 $q=Get-Content "$vm\native-live-v38\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.initial_response_held_until_observed -or $q.texture_scope -ne '1' -or $q.memory_cache -ne '1' -or !$q.continuous_delivery_gate_passed -or $q.ptl_four_tap_accepted -lt 1 -or $q.ptl_solid_accepted -lt 1){throw 'Scoped live evidence incomplete'}
 $report.phases+=@{phase='native-live-v38';passed=$true;cleanup_passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{Stop-Readiness;$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
