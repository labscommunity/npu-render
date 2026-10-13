$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-present-v3";$out="$r\vm\native-present-v3-trial";$source="$env:LOCALAPPDATA\Temp\npu-graphics-probe-0146a2388f25";$task='NpuRender-Finite-Native-Present-V3';$pm=$null;$registered=$false
$gate=& "$b\Release\nr_frame_present_probe.exe" --inspect-session|ConvertFrom-Json
if($LASTEXITCODE -or !$gate.ready_for_interactive_task){throw 'Physical screen probe requires the same signed-in unlocked console user'}
if(Test-Path $out){throw 'Trial evidence exists'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Renderer trial still active'}
$m=Get-Content "$b\build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or (Get-FileHash "$b\Release\nr_frame_present_probe.exe").Hash -ne $m.binary_sha256){throw 'Probe build mismatch'}
$proof=Get-Content "$r\vm\native-present-v2-frame.json" -Raw|ConvertFrom-Json
if(!$proof.manual_visual_verification -or !$proof.sole_indirect_at_receive -or (Get-FileHash "$r\vm\native-present-v2-frame.bin").Hash -ne $proof.packet_sha256){throw 'NPU source packet proof missing'}
$pmexe="$source\PresentMon-2.6.0-x64.exe"
if((Get-FileHash $pmexe).Hash -ne 'B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF' -or (Get-AuthenticodeSignature $pmexe).Status -ne 'Valid'){throw 'PresentMon identity mismatch'}
New-Item -ItemType Directory $out|Out-Null
Copy-Item "$r\vm\native-present-v2-frame.json" "$out\frame-provenance.json"
$job="$out\job.json";@{command=@("$b\Release\nr_frame_present_probe.exe","$r\vm\native-present-v2-frame.bin",'15');log="$out\probe.log"}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 $job
try {
 $pm=Start-Process $pmexe -ArgumentList @('--process_name','nr_frame_present_probe.exe','--write_display_metadata','--output_file',"$out\all-presentations.csv",'--no_console_stats','--date_time','--timed','25','--terminate_after_timed','--session_name','NpuRender-NativePresentV3') -RedirectStandardOutput "$out\presentmon.stdout.log" -RedirectStandardError "$out\presentmon.stderr.log" -PassThru;[void]$pm.Handle
 Start-Sleep -Seconds 2;$pm.Refresh();if($pm.HasExited){throw 'PresentMon stopped before probe'}
 $action=New-ScheduledTaskAction -Execute "$source\venv\Scripts\pythonw.exe" -Argument ('"'+$source+'\native_runner.py" "'+$job+'"')
 $principal=New-ScheduledTaskPrincipal -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings|Out-Null;$registered=$true;Start-ScheduledTask -TaskName $task
 $deadline=[DateTime]::UtcNow.AddSeconds(45)
 do{Start-Sleep -Milliseconds 250;$state=(Get-ScheduledTask -TaskName $task).State;$info=Get-ScheduledTaskInfo -TaskName $task}while(!(Test-Path "$out\probe.log") -and [DateTime]::UtcNow -lt $deadline -and $info.LastTaskResult -in @(0,267009,267011))
 [ordered]@{state=$state;last_result=$info.LastTaskResult;log_created=(Test-Path "$out\probe.log")}|ConvertTo-Json|Set-Content "$out\task-admission.json"
 if(!(Test-Path "$out\probe.log")){throw 'Interactive task did not launch the probe'}
 do{Start-Sleep -Milliseconds 500;$state=(Get-ScheduledTask -TaskName $task).State}while($state -eq 'Running' -and [DateTime]::UtcNow -lt $deadline)
 if($state -eq 'Running'){throw 'Finite probe timed out'}
 if(!$pm.WaitForExit(30000)){throw 'PresentMon timed out'};$pm.Refresh()
 $taskResult=(Get-ScheduledTaskInfo -TaskName $task).LastTaskResult
 Get-Content "$out\probe.log" -Tail 5
 if($taskResult -ne 0 -or $pm.ExitCode -ne 0){throw 'Probe or PresentMon failed'}
 $rows=@(Get-Content "$out\probe.log"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json});$ready=@($rows|Where-Object {$_.probe_ready});$done=@($rows|Where-Object {$_.completed})
 if($ready.Count -ne 1 -or $done.Count -ne 1 -or !$ready[0].backbuffer_roundtrip_exact -or $done[0].submitted_presents -lt 2){throw 'Probe completion receipts missing'}
 $owned=@(Import-Csv "$out\all-presentations.csv"|Where-Object {[int]$_.ProcessID -eq [int]$ready[0].pid})
 if(!$owned.Count){throw 'No probe presentation events'}
 $owned|Export-Csv "$out\probe-presentations.csv" -NoTypeInformation
 $modes=@($owned|Group-Object PresentMode|Select-Object Name,Count)
 [ordered]@{completed=$true;frame_provenance=$proof;probe_ready=$ready[0];probe_done=$done[0];presentation_rows=$owned.Count;presentation_modes=$modes;presentation_events_measured=$true;live_npu_rendering=$false;desktop_rendering_routed=$false;gpu_relief_measured=$false;full_desktop_offload=$false}|ConvertTo-Json -Depth 8|Set-Content "$out\report.json"
 $modes|ConvertTo-Json
} finally {
 if($registered){Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue}
 if($pm -and !$pm.HasExited){Stop-Process -Id $pm.Id -ErrorAction SilentlyContinue}
 if($pm){$pm.Dispose()}
}
