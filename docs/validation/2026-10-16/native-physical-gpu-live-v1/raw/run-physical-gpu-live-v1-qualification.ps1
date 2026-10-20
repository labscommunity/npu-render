$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\physical-gpu-live-v1-qualification"
if(Test-Path $out){throw 'GPU live coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;hardware_gpu_control=$true;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-npu-diagnostic';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Invoke-Step($name,$exe,$arguments,$seconds){
 $report.active_phase=$name;Save-Report
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$exe;$p.StartInfo.Arguments=$arguments;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Process did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit($seconds*1000)){$p.Kill();$p.WaitForExit();throw "$name deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);$report.steps+=@{name=$name;exit_code=$code};Save-Report;if($code -ne 0){throw "$name failed with exit $code"}}finally{$p.Dispose()}
}
function Controller($name,$file,$seconds){Invoke-Step $name 'powershell.exe' ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$vm+'\'+$file+'"') $seconds}
function Stop-Readiness($number){
 if(Test-Path "$vm/native-topology-v$number"){New-Item -ItemType File "$vm/native-topology-v$number/stop" -Force|Out-Null}
 $task=Get-ScheduledTask -TaskName "T8-NpuRender-InspectTopology-V$number" -ErrorAction SilentlyContinue
 if($task){if($task.Actions.Arguments -notlike "*native-topology-v$number*"){throw 'Readiness identity differs'};$until=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $task.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 200};Stop-ScheduledTask -TaskName $task.TaskName -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task.TaskName -Confirm:$false}
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(60)
 while($true){$q=Get-Content "$vm/ptl-infer-digest-v1-qualification/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'NPU diagnostic deadline'};Start-Sleep -Seconds 3}
 $report.preceding_diagnostic_passed=$q.passed
 $q=Get-Content "$vm/physical-gpu-desktop-v1/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Previous GPU route not restored'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 try{Controller 'preflight' 'start-native-topology-v106.ps1' 45}catch{
  $state=Get-Content "$vm/native-topology-v106/report.json" -Raw|ConvertFrom-Json
  if(!$state.completed -or $state.screen_saver_settings.secure -ne $false -or $state.after_screen_saver_close.input_name -ne 'Screen-saver'){throw}
  Controller 'nonsecure-saver' 'dismiss-native-saver-v31.ps1' 30
 }
 Stop-Readiness 106
 Controller 'readiness' 'start-native-topology-v107.ps1' 45
 Controller 'live' 'run-physical-gpu-live-v1.ps1' 1050
 $q=Get-Content "$vm/physical-gpu-live-v1/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.repeated_input_execution_passed -or !$q.actual_hardware_gpu_route -or $q.npu_receipts -ne 0 -or $q.hardware_child_receipts -ne 0 -or $q.provider_generations -ne 0){throw 'GPU live proof incomplete'}
 $report.cleanup_passed=$true
 Invoke-Step 'input-analysis' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\analyze-repeated-input-v2.py" --trial "'+$vm+'\physical-gpu-live-v1" --output "'+$vm+'\physical-gpu-repeated-input-v1-analysis"') 90
 $q=Get-Content "$vm/physical-gpu-repeated-input-v1-analysis/report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or @($q.controlled_events).Count -ne 40){throw 'Repeated input analysis incomplete'}
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 Stop-Readiness 106;Stop-Readiness 107
 $transaction="$vm/physical-gpu-desktop-v2"
 if(Test-Path "$transaction/report.json"){
  $q=Get-Content "$transaction/report.json" -Raw|ConvertFrom-Json
  if(!$q.completed -or !$q.cleanup_passed){New-Item -ItemType File "$transaction/stop" -Force|Out-Null;$deadline=[DateTime]::UtcNow.AddSeconds(100);do{Start-Sleep -Seconds 2;$q=Get-Content "$transaction/report.json" -Raw|ConvertFrom-Json}while((!$q.completed -or !$q.cleanup_passed) -and [DateTime]::UtcNow -lt $deadline)
   if((!$q.completed -or !$q.cleanup_passed) -and (Test-Path "$transaction/ownership.json")){& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$transaction/recover.ps1" -Transaction $transaction *> "$out/recovery.log"}
  }
 }
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
