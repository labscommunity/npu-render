$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-combined-desktop-v15"
if(Test-Path $out){throw 'Combined desktop coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-full-worker';phases=@()}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\atomic_lab_report_v1.ps1")))
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Controller([string]$file,[string]$name,[string[]]$extra=@()){
 $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\'+$file+'"'))+$extra
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName='powershell.exe';$p.StartInfo.Arguments=($arguments -join ' ');$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Controller did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit(1200000)){$p.Kill();$p.WaitForExit();throw "$name controller deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);if($code -ne 0){throw "$name controller failed with exit $code"}}finally{$p.Dispose()}
}
function Stop-Preflight{
 if(Test-Path "$vm\native-topology-v127"){New-Item -ItemType File "$vm\native-topology-v127\stop" -Force|Out-Null}
 $preflight=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V127 -ErrorAction SilentlyContinue
 if($preflight){if($preflight.Actions.Arguments -notlike '*native-topology-v127*'){throw 'Preflight task identity differs'};$until=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $preflight.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $preflight.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $preflight.TaskName -Confirm:$false}
}
try{
 $report.active_phase='waiting-for-combined-render-comparison';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(120)
 while($true){$q=Get-Content "$vm\ptl-worker-v26-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Turbo renderer comparison deadline'};Start-Sleep -Seconds 3}
 if(!$q.passed){throw 'Turbo renderer qualification incomplete'}
 $q=Get-Content "$vm\ptl-native-worker-v26-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.passed -or !$q.all_worker_v19_outputs_preserved -or $q.worker_sha256 -ne '6e28d3cf151450ef802a85d0b01b81b36d8e9aa13afbfba1c75bd57a13826bd0'){throw 'Turbo renderer image proof incomplete'}
 $comparison=Get-Content "$vm\turbo-render-comparison-v2\report.json" -Raw|ConvertFrom-Json
 if(!$comparison.completed -or !$comparison.passed -or $comparison.summary.pairs -ne 1944 -or $comparison.summary.new_parent_ms -ge .95*$comparison.summary.old_parent_ms){throw 'Turbo renderer lacks five percent aggregate paired improvement'}
 $report.paired_turbo_renderer=$comparison.summary
 $report.active_phase='waiting-for-combined-render-recovery';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(20)
 while($true){$q=Get-Content "$vm\turbo-render-provider-recovery-v1-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Turbo renderer recovery deadline'};Start-Sleep -Seconds 3}
 if(!$q.passed){throw 'Turbo renderer recovery not qualified'}
 $q=Get-Content "$vm\turbo-render-provider-recovery-v1\report.json" -Raw|ConvertFrom-Json
 if(!$q.passed -or @($q.phases).Count -ne 2 -or $q.worker_sha256 -ne '6e28d3cf151450ef802a85d0b01b81b36d8e9aa13afbfba1c75bd57a13826bd0'){throw 'Turbo renderer recovery proof differs'}
 $prior=Get-Content "$vm\native-desktop-admission-v72\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'GPU baseline not restored'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $prior=Get-Content "$vm\native-registration-v51\report.json" -Raw|ConvertFrom-Json
 if(!$prior.completed -or !$prior.cleanup_passed -or !$prior.memory_integrity_after){throw 'Previous registration cleanup incomplete'}
 $report.active_phase='pre-registration-readiness';Save-Report
 try{Controller 'start-native-topology-v127.ps1' 'preflight'}catch{
  $state=Get-Content "$vm\native-topology-v127\report.json" -Raw|ConvertFrom-Json
  if(!$state.completed -or $state.screen_saver_settings.secure -ne $false -or $state.after_screen_saver_close.input_name -ne 'Screen-saver'){throw}
  Controller 'dismiss-native-saver-v38.ps1' 'nonsecure-saver'
 }
 Controller 'start-native-topology-v128.ps1' 'readiness'
 Stop-Preflight
 $report.phases+=@{phase='pre-registration-readiness';passed=$true}
 $report.active_phase='registered-d3d';Save-Report
 Controller 'run-native-registration-v52.ps1' 'registration' @('-Mode','render')
 $q=Get-Content "$vm\native-registration-v52\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 Controller 'run-ptl-native-live-v27.ps1' 'live'
 $q=Get-Content "$vm\ptl-native-live-v27\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='timing-analysis';Save-Report
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-native-latency-v25.py"'),'--root',('"'+$vm+'"'),'--output',('"'+$vm+'\native-latency-v27-analysis"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\analysis.stdout.log" -RedirectStandardError "$out\analysis.stderr.log"
 try{if($analysis.ExitCode){throw 'analysis failed; full stderr retained'}}finally{$analysis.Dispose()}
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-native-presentation-v24.py"'),'--root',('"'+$vm+'"'),'--output',('"'+$vm+'\native-presentation-v24-analysis"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\presentation.stdout.log" -RedirectStandardError "$out\presentation.stderr.log"
 try{if($analysis.ExitCode){throw 'presentation failed; full stderr retained'}}finally{$analysis.Dispose()}
 $analysis=Start-Process "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" -ArgumentList @(('"'+$vm+'\analyze-repeated-input-v2.py"'),'--trial',('"'+$vm+'\native-live-v63"'),'--output',('"'+$vm+'\native-repeated-input-v10-analysis"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\repeat-analysis.stdout.log" -RedirectStandardError "$out\repeat-analysis.stderr.log"
 try{if(!$analysis.WaitForExit(60000)){$analysis.Kill();$analysis.WaitForExit();throw 'Repeated analysis deadline'};$analysis.Refresh();if($analysis.ExitCode){throw 'Repeated input analysis failed'}}finally{$analysis.Dispose()}
 $report.phases+=@{phase='timing-analysis';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 Stop-Preflight
 if(Test-Path "$vm\native-topology-v128"){New-Item -ItemType File "$vm\native-topology-v128\stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V128 -ErrorAction SilentlyContinue
 if($task){if($task.Actions.Arguments -notlike '*native-topology-v128*'){throw 'Readiness task identity differs'};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
