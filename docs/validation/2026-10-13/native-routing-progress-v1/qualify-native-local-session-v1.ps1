$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-local-session-v1";$root="$env:ProgramData\T8-NpuRender-SessionInspectV1";$task='T8-NpuRender-SessionInspectV1';$artifact="$r\vm\native-local-session-v1-qualification"
if((Test-Path $root) -or (Test-Path $artifact) -or (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue)){throw 'Owned diagnostic identity exists'}
$build=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$build.clean_build -or (Get-FileHash "$b\Release\nr_native_local_session.exe").Hash -ne $build.binary_sha256.'nr_native_local_session.exe'){throw 'Diagnostic build mismatch'}
$registered=$false;$created=$false;$cleanup=[ordered]@{completed=$false;task_removed=$false;owned_processes_removed=$false;directory_removed=$false;desktop_changed=$false;driver_installed=$false}
try{
 New-Item -ItemType Directory $root|Out-Null;$created=$true
 $acl=New-Object System.Security.AccessControl.DirectorySecurity;$acl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;LS)');Set-Acl $root -AclObject $acl
 Copy-Item "$b\Release\nr_native_local_session.exe" $root
 New-Item -ItemType Directory "$root\output"|Out-Null
 $acl=New-Object System.Security.AccessControl.DirectorySecurity;$acl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;LS)');Set-Acl "$root\output" -AclObject $acl
 $action=New-ScheduledTaskAction -Execute "$root\nr_native_local_session.exe" -Argument ('"'+$root+'\output\inspection.json"')
 $principal=New-ScheduledTaskPrincipal -UserId 'S-1-5-19' -LogonType ServiceAccount
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings|Out-Null;$registered=$true
 Start-ScheduledTask -TaskName $task;$until=[DateTime]::UtcNow.AddSeconds(15)
 do{Start-Sleep -Milliseconds 200;$state=(Get-ScheduledTask -TaskName $task).State}while((!(Test-Path "$root\output\inspection.json") -or $state -eq 'Running') -and [DateTime]::UtcNow -lt $until)
 $info=Get-ScheduledTaskInfo -TaskName $task;if($state -eq 'Running' -or $info.LastTaskResult -ne 0){throw 'LocalService session inspection failed'}
 $evidence=Get-Content "$root\output\inspection.json" -Raw|ConvertFrom-Json
 if(!$evidence.local_service_session_inspection -or $evidence.process_session -ne 0){throw 'LocalService identity evidence absent'}
 [ordered]@{completed=$true;passed=$true;last_task_result=$info.LastTaskResult;inspection=$evidence}|ConvertTo-Json -Depth 5|Set-Content "$root\output\report.json";Get-Content "$root\output\report.json"
}finally{
 if($registered){Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task -Confirm:$false}
 $cleanup.task_removed=!(Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue)
 foreach($p in @(Get-CimInstance Win32_Process -Filter "Name='nr_native_local_session.exe'")){if($p.ExecutablePath -eq "$root\nr_native_local_session.exe"){Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue}}
 $cleanup.owned_processes_removed=@(Get-CimInstance Win32_Process -Filter "Name='nr_native_local_session.exe'"|Where-Object ExecutablePath -eq "$root\nr_native_local_session.exe").Count -eq 0
 if($created){Copy-Item "$root\output" $artifact -Recurse;if($cleanup.owned_processes_removed){Remove-Item $root -Recurse -Force}}
 $cleanup.directory_removed=!(Test-Path $root);$cleanup.completed=$true;$cleanup|ConvertTo-Json|Set-Content "$artifact\cleanup.json";$cleanup|ConvertTo-Json
}
