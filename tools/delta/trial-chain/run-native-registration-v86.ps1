param([ValidateSet('stage','render')][string]$Mode='stage')
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$package="$r\vm\native-driver-package-v41";$out=if($Mode -eq 'stage'){"$r\vm\native-registration-stage-v41"}else{"$r\vm\native-registration-v86"};$task='T8-NpuRender-NativeRecovery-v86'
$sources=Get-Content "$r\vm\native-registration-v86-source.json" -Raw|ConvertFrom-Json
foreach($f in $sources.psobject.Properties){if((Get-FileHash "$r\vm\$($f.Name)").Hash -ne $f.Value){throw 'Registration source mismatch'}}
foreach($path in @('native-quad-abba-v1','quad-pipeline-v1-qualification','native-ordered-replay-v29-quad-rollback')){$q=Get-Content "$r\vm\$path\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Quad promotion prerequisites failed'}}
foreach($path in @('native-provider-process-v2','native-ordered-replay-v39-provider-soak')){$q=Get-Content "$r\vm\$path\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Provider promotion prerequisites failed'}}
foreach($path in @('native-duration-components-v2','native-combined-v3')){$q=Get-Content "$r\vm\$path\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Duration renderer prerequisites failed'}}
$full=Get-Content "$r\vm\native-texture-full-v1-evidence\report.json" -Raw|ConvertFrom-Json
if(!$full.completed -or !$full.passed -or !$full.at_least_ten_percent_reduction){throw 'Complete scoped comparison not qualified'}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevated authorized controller required'}
if((Test-Path $out) -or (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue)){throw 'Transaction or recovery already exists'}
if(@(Get-Process nr_registered*,nr_native* -ErrorAction SilentlyContinue).Count){throw 'Rendering experiment active'}
foreach($candidate in @('alpha-copy-v3-qualification','alpha-copy-integrated-v2','native-ordered-replay-v24-alpha-fixed','native-ordered-replay-v24-alpha-fixed','native-ordered-replay-v25-alpha-fixed','native-ordered-replay-v26-primary-alpha-fixed')){$q=Get-Content "$r\vm\$candidate\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw "Alpha prerequisite failed: $candidate"}}
$manifest=Get-Content "$package\sha256.json" -Raw|ConvertFrom-Json
foreach($f in $manifest){if($f.name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash "$package\$($f.name)").Hash -ne $f.sha256){throw 'Package mismatch'}}
$meta=Get-Content "$package\report.json" -Raw|ConvertFrom-Json
if(!$meta.prepared -or $meta.hardware_id -ne 'ROOT\NPURENDER_NATIVE_PROBE' -or $meta.version -ne '0.0.1341.0'){throw 'Unexpected package'}
$prior=@(Get-PnpDevice -PresentOnly -Class Display|ForEach-Object {$ids=@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data);if($ids -contains $meta.hardware_id){throw 'Diagnostic adapter exists'};[ordered]@{instance=$_.InstanceId;status=$_.Status;hardware_ids=$ids}})
if(!$prior.Count -or @($prior|Where-Object status -ne 'OK').Count){throw 'Existing display adapter unhealthy'}
if(@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq 'nr_native_probe.inf'}).Count){throw 'Native probe package already staged'}
$vbs=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard
if($vbs.VirtualizationBasedSecurityStatus -ne 2 -or $vbs.SecurityServicesRunning -notcontains 2){throw 'Expected running memory integrity'}
$boot=(& bcdedit /enum '{current}') -join "`n";if($LASTEXITCODE -or $boot -notmatch '(?im)^testsigning\s+Yes\s*$'){throw 'Existing test-signing boot mode required'}
New-Item -ItemType Directory $out|Out-Null
& icacls.exe $out /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' > "$out\directory-acl.log";if($LASTEXITCODE){throw 'Recovery directory protection failed'}
Copy-Item "$r\vm\recover-native-registration-v86.ps1" "$out\recover.ps1"
$ownership=[ordered]@{schema=1;hardware_id=$meta.hardware_id;version=$meta.version;thumbprint=$meta.signer_thumbprint;inf_sha256=(Get-FileHash "$package\nr_native_probe.inf").Hash;devcon="$r\dependencies\wdk-tools\devcon.exe";devcon_sha256=(Get-FileHash "$r\dependencies\wdk-tools\devcon.exe").Hash;physical_display_before=$prior;added_stores=@();controller_pid=$PID;mode=$Mode;created=[DateTime]::UtcNow.ToString('o')}
function Save-Ownership {$ownership|ConvertTo-Json -Depth 6|Set-Content "$out\ownership.tmp";Move-Item "$out\ownership.tmp" "$out\ownership.json" -Force}
Save-Ownership
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');mode=$Mode;completed=$false;passed=$false;controller_pid=$PID;memory_integrity_before=$true;boot_settings_changed=$false;physical_display_before=$prior;desktop_offloaded=$false}
function Save-Report {$report|ConvertTo-Json -Depth 7|Set-Content "$out\report.json"}
function Run-OwnedTool([string]$File,[string[]]$Arguments,[string]$Name,[int]$Seconds=60){
 $quoted=@($Arguments|ForEach-Object {'"'+$_.Replace('"','\"')+'"'})
 $process=Start-Process -FilePath $File -ArgumentList $quoted -PassThru -NoNewWindow -RedirectStandardOutput "$out\$Name.stdout.log" -RedirectStandardError "$out\$Name.stderr.log"
 $null=$process.Handle
 if(!$process.WaitForExit($Seconds*1000)){$process.Kill();$process.WaitForExit();throw "Owned $Name tool timed out"}
 $process.Refresh();$code=$process.ExitCode;$process.Dispose();return $code
}
Save-Report;$recoveryArmed=$false
try {
 $action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File `"$out\recover.ps1`" -Root `"$out`""
 $triggers=@((New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(7)),(New-ScheduledTaskTrigger -AtStartup))
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 3) -MultipleInstances IgnoreNew -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 $identity=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
 Register-ScheduledTask -TaskName $task -Action $action -Trigger $triggers -Settings $settings -Principal $identity|Out-Null
 $recoveryArmed=$true;$report.recovery_task_armed=$true;Save-Report
 $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new("$package\test-signing.cer")
 if($cert.Thumbprint -ne $ownership.thumbprint){throw 'Certificate identity mismatch'}
 foreach($name in 'Root','TrustedPublisher'){
  if(Test-Path "Cert:\LocalMachine\$name\$($ownership.thumbprint)"){continue}
  $ownership.added_stores+= $name;Save-Ownership
  $store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine');$store.Open('ReadWrite');try{$store.Add($cert)}finally{$store.Close()}
  if(!(Test-Path "Cert:\LocalMachine\$name\$($ownership.thumbprint)")){throw 'Diagnostic certificate trust unavailable'}
 }
 $report.stage_exit=Run-OwnedTool "$env:windir\System32\pnputil.exe" @('/add-driver',"$package\nr_native_probe.inf") 'stage';Save-Report;if($report.stage_exit){throw 'Package staging failed'}
 $staged=@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq 'nr_native_probe.inf' -and $_.Version -eq $meta.version})
 if($staged.Count -ne 1 -or $staged[0].Driver -notmatch '^oem\d+\.inf$' -or (Get-FileHash "$env:windir\INF\$($staged[0].Driver)").Hash -ne $ownership.inf_sha256){throw 'Staged package identity mismatch'}
 $report.published_inf=$staged[0].Driver;$ownership.published_inf=$staged[0].Driver;Save-Ownership;Save-Report
 if($Mode -eq 'render'){
  $stageProof=Get-Content "$r\vm\native-registration-stage-v41\report.json" -Raw|ConvertFrom-Json
  if(!$stageProof.passed -or !$stageProof.cleanup_passed){throw 'Staging and recovery not qualified'}
  $report.install_exit=Run-OwnedTool $ownership.devcon @('install',"$package\nr_native_probe.inf",$meta.hardware_id) 'install';Save-Report;if($report.install_exit){throw 'Diagnostic adapter installation failed or requested restart'}
  $devices=@(Get-PnpDevice -PresentOnly -Class Display|Where-Object {@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data) -contains $meta.hardware_id})
  if($devices.Count -ne 1 -or $devices[0].Status -ne 'OK'){throw 'Native adapter not healthy'}
  $report.instance=$devices[0].InstanceId
  $key=($devices[0]|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_Driver).Data
  if($key -notmatch '^\{4d36e968-e325-11ce-bfc1-08002be10318\}\\\d{4}$'){throw 'Unexpected adapter class registration'}
  $registration=Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$key"
  $modules=@($registration.UserModeDriverName|Select-Object -Unique);$dll=$modules[0]
  $expected=($manifest|Where-Object name -eq 'nr_native_probe_umd.dll').sha256
  if($modules.Count -ne 1 -or $registration.DriverVersion -ne $meta.version -or $registration.InfPath -ne $report.published_inf -or !$dll.StartsWith("$env:windir\System32\DriverStore\FileRepository\",[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($dll) -ne 'nr_native_probe_umd.dll' -or (Get-FileHash $dll).Hash -ne $expected){throw 'Installed adapter identity mismatch'}
  $report.installed_umd_sha256=$expected;Save-Report
  $probeArguments=@("$r\vm\qualify-native-registered-v86.py",'--worker',"$r\builds\native-tree-v47\Release\nr_registered_local_indexed_worker.exe",'--client',"$r\builds\native-registered-v1\Release\nr_native_registered_probe.exe",'--dll',$dll,'--package',$package,'--runtime',"$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs",'--cache',"$r\vm\model-cache-2026-4-v209",'--output',"$out\qualification")
  $report.qualification_exit=Run-OwnedTool "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" $probeArguments 'qualification' 150;Save-Report;if($report.qualification_exit){throw 'Registered NPU qualification failed'}
  $q=Get-Content "$out\qualification\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.registered_adapter_tested){throw 'Registered execution evidence absent'}
  if(!$q.automatic_admission -or $q.consumer_started_suspended -or !$q.automatic_broker_cleanup_passed -or $q.consumer_elevated -or $q.consumer_session -lt 1 -or !$q.pipe_server_creation_access_denied -or !$q.owned_process_and_token_acls_restored_exactly){throw 'Interactive graphics admission proof incomplete'}
  $report.registered_npu_qualified=$true
 }
 $report.operations_passed=$true
}catch{$report.error=$_.Exception.Message}
finally {
 if($recoveryArmed){
  try {
   $report.recovery_exit=Run-OwnedTool "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',"$out\recover.ps1",'-Root',$out) 'recovery' 150
   $cleanup=Get-Content "$out\recovery-report.json" -Raw|ConvertFrom-Json;$report.cleanup_passed=($report.recovery_exit -eq 0 -and $cleanup.passed)
  }catch{$report.cleanup_error=$_.Exception.Message;$report.cleanup_passed=$false}
 }
 try{$after=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard;$report.memory_integrity_after=($after.VirtualizationBasedSecurityStatus -eq 2 -and $after.SecurityServicesRunning -contains 2)}catch{$report.memory_integrity_after=$false}
 $report.passed=([bool]$report.operations_passed -and [bool]$report.cleanup_passed -and $report.memory_integrity_after);$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
