$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-pair-stage-v1";$task='T8-NpuRender-NativePairRecovery-v1';$thumb='3112355AF6B6C6265E9B849662D3E02F6797343A'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevated controller required'}
if((Test-Path $out) -or (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue)){throw 'Transaction identity exists'}
if(@(Get-Process nr_registered*,nr_native_service_probe,nr_native_local_session -ErrorAction SilentlyContinue).Count){throw 'Another rendering experiment active'}
$packages=@(@{folder='native-driver-package-v2';inf='nr_native_probe.inf';hardware_id='ROOT\NPURENDER_NATIVE_PROBE';version='0.0.1302.0'},@{folder='native-idd-package-v1';inf='nr_native_indirect_probe.inf';hardware_id='ROOT\NPURENDER_NATIVE_INDIRECT_V1';version='0.0.1301.0'})
foreach($p in $packages){
 $directory="$r\vm\$($p.folder)";$m=Get-Content "$directory\report.json" -Raw|ConvertFrom-Json
 if(!$m.prepared -or $m.hardware_id -ne $p.hardware_id -or $m.version -ne $p.version -or $m.signer_thumbprint -ne $thumb){throw 'Package metadata mismatch'}
 foreach($file in @(Get-Content "$directory\sha256.json" -Raw|ConvertFrom-Json)){if($file.name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash "$directory\$($file.name)").Hash -ne $file.sha256){throw 'Package file mismatch'}}
 $p.inf_sha256=(Get-FileHash "$directory\$($p.inf)").Hash
 if(@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq $p.inf}).Count){throw 'Package was already staged'}
}
$before=@(Get-PnpDevice -PresentOnly -Class Display|ForEach-Object {$ids=@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data);foreach($p in $packages){if($ids -contains $p.hardware_id){throw 'Owned adapter already exists'}};[ordered]@{instance=$_.InstanceId;status=$_.Status;hardware_ids=$ids}})
if(!$before.Count -or @($before|Where-Object status -ne 'OK').Count){throw 'Original display health unavailable'}
$vbs=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard;if($vbs.VirtualizationBasedSecurityStatus -ne 2 -or $vbs.SecurityServicesRunning -notcontains 2){throw 'Expected running memory integrity'}
New-Item -ItemType Directory $out|Out-Null
& icacls.exe $out /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' > "$out\directory-acl.log";if($LASTEXITCODE){throw 'Ownership directory protection failed'}
Copy-Item "$r\vm\recover-native-pair-v1.ps1" "$out\recover.ps1"
$ownership=[ordered]@{schema=2;packages=$packages;thumbprint=$thumb;devcon="$r\dependencies\wdk-tools\devcon.exe";devcon_sha256=(Get-FileHash "$r\dependencies\wdk-tools\devcon.exe").Hash;physical_display_before=$before;added_stores=@();controller_pid=$PID;created=[DateTime]::UtcNow.ToString('o');mode='stage-only'}
function Save-Ownership{$ownership|ConvertTo-Json -Depth 7|Set-Content "$out\ownership.tmp";Move-Item "$out\ownership.tmp" "$out\ownership.json" -Force}
Save-Ownership
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;mode='stage-only';devices_installed=$false;display_topology_changed=$false;boot_settings_changed=$false;memory_integrity_before=$true;staged=@();desktop_offloaded=$false}
function Save-Report{$report|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"}
function Run-Owned([string]$file,[string[]]$arguments,[string]$label,[int]$seconds=90){$quoted=@($arguments|ForEach-Object {'"'+$_.Replace('"','\"')+'"'});$process=Start-Process $file -ArgumentList $quoted -PassThru -NoNewWindow -RedirectStandardOutput "$out\$label.stdout.log" -RedirectStandardError "$out\$label.stderr.log";[void]$process.Handle;if(!$process.WaitForExit($seconds*1000)){$process.Kill();$process.WaitForExit();throw "Owned $label tool timed out"};$process.Refresh();$code=$process.ExitCode;$process.Dispose();return $code}
Save-Report;$armed=$false
try{
 $action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File `"$out\recover.ps1`" -Root `"$out`""
 $triggers=@((New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(7)),(New-ScheduledTaskTrigger -AtStartup));$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 5) -MultipleInstances IgnoreNew -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries;$identity=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
 Register-ScheduledTask -TaskName $task -Action $action -Trigger $triggers -Settings $settings -Principal $identity|Out-Null;$armed=$true;$report.recovery_task_armed=$true;Save-Report
 $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new("$r\vm\native-driver-package-v2\test-signing.cer");if($cert.Thumbprint -ne $thumb){throw 'Certificate mismatch'}
 foreach($name in @('Root','TrustedPublisher')){if(!(Test-Path "Cert:\LocalMachine\$name\$thumb")){$ownership.added_stores+=$name;Save-Ownership;$store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine');$store.Open('ReadWrite');try{$store.Add($cert)}finally{$store.Close()};if(!(Test-Path "Cert:\LocalMachine\$name\$thumb")){throw 'Temporary trust unavailable'}}}
 foreach($p in $packages){
  $code=Run-Owned "$env:windir\System32\pnputil.exe" @('/add-driver',"$r\vm\$($p.folder)\$($p.inf)") $p.folder;if($code){throw 'Package staging failed'}
  $staged=@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq $p.inf -and $_.Version -eq $p.version})
  if($staged.Count -ne 1 -or $staged[0].Driver -notmatch '^oem\d+\.inf$' -or (Get-FileHash "$env:windir\INF\$($staged[0].Driver)").Hash -ne $p.inf_sha256){throw 'Staged package identity mismatch'}
  $report.staged+=@{inf=$p.inf;published_inf=$staged[0].Driver;version=$p.version};Save-Report
 }
 $report.operations_passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 if($armed){try{$code=Run-Owned "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',"$out\recover.ps1",'-Root',$out) 'recovery' 240;$cleanup=Get-Content "$out\recovery-report.json" -Raw|ConvertFrom-Json;$report.cleanup_passed=($code -eq 0 -and $cleanup.passed)}catch{$report.cleanup_passed=$false;$report.cleanup_error=$_.Exception.Message}}
 try{$after=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard;$report.memory_integrity_after=($after.VirtualizationBasedSecurityStatus -eq 2 -and $after.SecurityServicesRunning -contains 2)}catch{$report.memory_integrity_after=$false}
 $report.passed=([bool]$report.operations_passed -and [bool]$report.cleanup_passed -and $report.memory_integrity_after);$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
