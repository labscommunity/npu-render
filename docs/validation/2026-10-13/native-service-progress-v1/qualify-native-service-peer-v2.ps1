$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$name='native-service-peer-v2';$b="$r\builds\$name";$root="$env:ProgramData\T8-NpuRender-ServicePeerV2";$artifact="$r\vm\$name-qualification"
if((Test-Path $root) -or (Test-Path $artifact)){throw 'Diagnostic path exists'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_native_service_probe -ErrorAction SilentlyContinue).Count){throw 'Another diagnostic process active'}
foreach($kind in @('denied','granted')){if(Get-ScheduledTask -TaskName "T8-NpuRender-ServicePeerV2-$kind" -ErrorAction SilentlyContinue){throw 'Diagnostic task exists'}}
$expected=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
foreach($n in @('nr_registered_local_indexed_worker.exe','nr_native_service_probe.exe')){if((Get-FileHash "$b\Release\$n").Hash -ne $expected.binary_sha256.$n){throw 'Binary hash mismatch'}}
$cleanup=[ordered]@{completed=$false;task_cleanup=$false;owned_process_cleanup=$false;directory_removed=$false;driver_installed=$false;desktop_changed=$false}
$tasks=@();$created=$false
try{
 New-Item -ItemType Directory $root|Out-Null;$created=$true
 $acl=New-Object System.Security.AccessControl.DirectorySecurity
 $acl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;LS)')
 Set-Acl -LiteralPath $root -AclObject $acl
 foreach($n in @('nr_registered_local_indexed_worker.exe','nr_native_service_probe.exe')){Copy-Item "$b\Release\$n" "$root\$n";if((Get-FileHash "$root\$n").Hash -ne $expected.binary_sha256.$n){throw 'Copied hash mismatch'}}
 foreach($kind in @('denied','granted')){
  $out="$root\$kind";New-Item -ItemType Directory $out|Out-Null
  $dataAcl=New-Object System.Security.AccessControl.DirectorySecurity
  $dataAcl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;LS)');Set-Acl -LiteralPath $out -AclObject $dataAcl
  $task="T8-NpuRender-ServicePeerV2-$kind"
  $action=New-ScheduledTaskAction -Execute "$root\nr_native_service_probe.exe" -Argument ('"'+$out+'"') -WorkingDirectory $root
  $principal=New-ScheduledTaskPrincipal -UserId 'S-1-5-19' -LogonType ServiceAccount
  $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
  Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Description 'Finite owned LocalService NPU diagnostic; no desktop or driver changes'|Out-Null;$tasks+=$task
  $arguments=@("$r\vm\qualify-native-service-peer-v2.py",'--worker',"$root\nr_registered_local_indexed_worker.exe",'--client',"$root\nr_native_service_probe.exe",'--runtime',"$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs",'--cache',"$r\vm\model-cache-2026-4-v209",'--output',$out,'--build',$b,'--task',$task)
  if($kind -eq 'granted'){$arguments+='--grant-process-access'}
  & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" @arguments
  if($LASTEXITCODE){throw "LocalService $kind qualification failed"}
  $info=Get-ScheduledTaskInfo -TaskName $task
  [ordered]@{last_task_result=$info.LastTaskResult;last_run_time=$info.LastRunTime.ToUniversalTime().ToString('o');local_service=$true}|ConvertTo-Json|Set-Content "$out\task-result.json"
  Unregister-ScheduledTask -TaskName $task -Confirm:$false
 }
}finally{
 foreach($task in $tasks){$t=Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue;if($t){if($t.State -eq 'Running'){Stop-ScheduledTask -TaskName $task};Unregister-ScheduledTask -TaskName $task -Confirm:$false}}
 $cleanup.task_cleanup=@($tasks|Where-Object {Get-ScheduledTask -TaskName $_ -ErrorAction SilentlyContinue}).Count -eq 0
 # Only processes executing the binaries copied into this newly owned folder qualify for termination.
 foreach($process in @(Get-CimInstance Win32_Process -Filter "Name='nr_native_service_probe.exe' OR Name='nr_registered_local_indexed_worker.exe'")){
  if($process.ExecutablePath -and $process.ExecutablePath.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue}
 }
 $remaining=@(Get-CimInstance Win32_Process -Filter "Name='nr_native_service_probe.exe' OR Name='nr_registered_local_indexed_worker.exe'"|Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)})
 $cleanup.owned_process_cleanup=$remaining.Count -eq 0
 if($created){New-Item -ItemType Directory $artifact|Out-Null;foreach($kind in @('denied','granted')){if(Test-Path "$root\$kind"){Copy-Item "$root\$kind" "$artifact\$kind" -Recurse}};if($cleanup.owned_process_cleanup){Remove-Item $root -Recurse -Force}}
 $cleanup.directory_removed=!(Test-Path $root);$cleanup.completed=$true;$cleanup|ConvertTo-Json|Set-Content "$artifact\cleanup.json";$cleanup|ConvertTo-Json
}
if(!$cleanup.task_cleanup -or !$cleanup.owned_process_cleanup -or !$cleanup.directory_removed){throw 'Owned diagnostic cleanup incomplete'}
