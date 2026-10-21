param([ValidateSet('preflight','admit')][string]$Mode='preflight')
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-desktop-admission-v97";$task='T8-NpuRender-NativeDesktopRecovery-v97';$thumb='3112355AF6B6C6265E9B849662D3E02F6797343A'
$probe="$r\builds\native-present-v6\Release\nr_frame_present_probe.exe"
$proof=Get-Content "$r\builds\native-present-v6\build-report.json" -Raw|ConvertFrom-Json
if(!$proof.clean_build -or (Get-FileHash $probe).Hash -ne $proof.binary_sha256){throw 'Session inspector identity mismatch'}
$source=Get-Content "$r\vm\native-desktop-v97-source.json" -Raw|ConvertFrom-Json
foreach($f in $source.psobject.Properties){if((Get-FileHash "$r\vm\$($f.Name)").Hash -ne $f.Value){throw 'Native controller source mismatch'}}
$gate=(& $probe --inspect-session)|ConvertFrom-Json;if($LASTEXITCODE){throw 'Console inspection failed'}
$preflight=[ordered]@{observed=[DateTime]::UtcNow.ToString('o');mode=$Mode;session_gate=$gate;ready=[bool]$gate.ready_for_interactive_task;admission_attempted=$false;driver_installed=$false;desktop_offloaded=$false}
if(!$gate.ready_for_interactive_task -or $Mode -eq 'preflight'){$preflight|ConvertTo-Json -Depth 5|Set-Content "$r\vm\native-desktop-v97-preflight.json";$preflight|ConvertTo-Json -Depth 5;return}
$registered=Get-Content "$r\vm\native-registration-v69\report.json" -Raw|ConvertFrom-Json
$broker=Get-Content "$r\vm\native-broker-v3-qualification\report.json" -Raw|ConvertFrom-Json
$observer=Get-Content "$r\vm\native-observer-v1-qualification\report.json" -Raw|ConvertFrom-Json
if(!$registered.passed -or !$registered.registered_npu_qualified -or !$registered.cleanup_passed -or !$broker.passed -or !$broker.owned_process_acls_restored_exactly -or !$observer.passed){throw 'Native prerequisites are not qualified'}
foreach($candidate in @('point-crop-v1-qualification','point-crop-integrated-v1','crop-transition-v2-qualification','native-ordered-replay-v15-point','native-ordered-replay-v16-point','native-ordered-replay-v17-point')){
 $qualified=Get-Content "$r\vm\$candidate\report.json" -Raw|ConvertFrom-Json;if(!$qualified.completed -or !$qualified.passed){throw "Unqualified point-copy prerequisite: $candidate"}
}
$replay=Get-Content "$r\vm\native-ordered-replay-v22-primary-full\report.json" -Raw|ConvertFrom-Json;if(!$replay.completed -or !$replay.passed){throw 'Primary recorded stream not qualified'}
foreach($candidate in @('alpha-copy-v3-qualification','alpha-copy-integrated-v2','native-ordered-replay-v23-alpha-fixed','native-ordered-replay-v24-alpha-fixed','native-ordered-replay-v25-alpha-fixed','native-ordered-replay-v26-primary-alpha-fixed')){$q=Get-Content "$r\vm\$candidate\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw "Alpha prerequisite failed: $candidate"}}
$visibleReplay=Get-Content "$r\vm\native-ordered-replay-v27-primary-visible\report.json" -Raw|ConvertFrom-Json;if(!$visibleReplay.completed -or !$visibleReplay.passed){throw 'Visible-window replay prerequisite failed'}
$frameIO=Get-Content "$r\vm\native-frame-io-v1\report.json" -Raw|ConvertFrom-Json;if(!$frameIO.completed -or !$frameIO.passed){throw 'Frame IO prerequisite failed'}
$recovery=Get-Content "$r\vm\native-desktop-recovery-v1b\report.json" -Raw|ConvertFrom-Json
if(!$recovery.passed -or !$recovery.watchdog_trigger_exercised -or !$recovery.owned_processes_stopped -or !$recovery.trace_directories_archived){throw 'Automatic native recovery is not qualified'}
$previous=Get-Content "$r\vm\native-desktop-admission-v42\report.json" -Raw|ConvertFrom-Json
if(!$previous.completed -or !$previous.cleanup_passed){throw 'Previous native cleanup incomplete'}
$publication=Get-Content "$r\vm\native-frame-publication-v2-qualification\report.json" -Raw|ConvertFrom-Json
if(!$publication.completed -or !$publication.passed -or $publication.source_sha256.'native_frame_publish_v1.py' -ne (Get-FileHash "$r\vm\native_frame_publish_v1.py").Hash){throw 'Atomic publication not qualified'}
$worker="$r\builds\native-tree-v43\Release\nr_registered_local_indexed_worker.exe"
$workerProof=Get-Content "$r\builds\native-tree-v43\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$workerProof.clean_build -or (Get-FileHash $worker).Hash -ne $workerProof.binary_sha256.'nr_registered_local_indexed_worker.exe'){throw 'NPU worker identity mismatch'}
$registeredWorker=Get-Content "$r\vm\native-registration-v69\qualification\report.json" -Raw|ConvertFrom-Json;if(!$registeredWorker.passed -or $registeredWorker.worker_binary_sha256 -ne (Get-FileHash $worker).Hash){throw 'Registered worker identity mismatch'}
foreach($name in @('T8-NpuRender-NativeTraceV1','T8-NpuRender-NativeIddV1')){if(Test-Path "$env:ProgramData\$name"){throw 'Native diagnostic directory already exists'}}
$boot=(& bcdedit /enum '{current}') -join "`n";if($LASTEXITCODE -or $boot -notmatch '(?im)^testsigning\s+Yes\s*$'){throw 'Existing test-signing mode required'}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevated controller required'}
if((Test-Path $out) -or (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue)){throw 'Transaction identity exists'}
if(@(Get-Process nr_registered*,nr_native_service_probe,nr_native_local_session -ErrorAction SilentlyContinue).Count){throw 'Another rendering experiment active'}
# The pinned topology inspector is read-only and may overlap this preflight.
# Live/static presentation probes remain excluded by executable and exact args.
$inspectPattern='^(?:"'+[regex]::Escape($probe)+'"|'+[regex]::Escape($probe)+')\s+--inspect-topology\s*$'
foreach($process in @(Get-CimInstance Win32_Process -Filter "Name = 'nr_frame_present_probe.exe'")){
 $pinned=Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue
 if(!$pinned){continue}
 try{
  [void]$pinned.Handle;if($pinned.HasExited){continue}
  if($process.ExecutablePath -ne $probe -or $process.CommandLine -notmatch $inspectPattern){
   $process|Select-Object ProcessId,SessionId,ExecutablePath,CommandLine,CreationDate|ConvertTo-Json -Depth 3
   throw 'Another presentation experiment active'
  }
 }finally{$pinned.Dispose()}
}
$iddProof=Get-Content "$r\vm\native-idd-package-v3\report.json" -Raw|ConvertFrom-Json
if(!$iddProof.prepared -or $iddProof.desktop_lease_max_ms -ne 2400000 -or $iddProof.owned_app_lease_max_ms -ne 120000){throw 'Indirect duration contract not qualified'}
$packages=@(@{folder='native-driver-package-v37';inf='nr_native_probe.inf';hardware_id='ROOT\NPURENDER_NATIVE_PROBE';version='0.0.1337.0'},@{folder='native-idd-package-v3';inf='nr_native_indirect_probe.inf';hardware_id='ROOT\NPURENDER_NATIVE_INDIRECT_V1';version='0.0.1503.0'})
foreach($p in $packages){
 $directory="$r\vm\$($p.folder)";$m=Get-Content "$directory\report.json" -Raw|ConvertFrom-Json
 if(!$m.prepared -or $m.hardware_id -ne $p.hardware_id -or $m.version -ne $p.version -or $m.signer_thumbprint -ne $thumb){throw 'Package metadata mismatch'}
 $files=Get-Content "$directory\sha256.json" -Raw|ConvertFrom-Json
 foreach($file in $files){if($file.name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash "$directory\$($file.name)").Hash -ne $file.sha256){throw 'Package file mismatch'}}
 $p.inf_sha256=(Get-FileHash "$directory\$($p.inf)").Hash
 if(@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq $p.inf}).Count){throw 'Package was already staged'}
}
$before=@(Get-PnpDevice -PresentOnly -Class Display|ForEach-Object {$ids=@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data);foreach($p in $packages){if($ids -contains $p.hardware_id){throw 'Owned adapter already exists'}};[ordered]@{instance=$_.InstanceId;status=$_.Status;hardware_ids=$ids}})
if(!$before.Count -or @($before|Where-Object status -ne 'OK').Count){throw 'Original display health unavailable'}
$vbs=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard;if($vbs.VirtualizationBasedSecurityStatus -ne 2 -or $vbs.SecurityServicesRunning -notcontains 2){throw 'Expected running memory integrity'}
New-Item -ItemType Directory $out|Out-Null
& icacls.exe $out /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' > "$out\directory-acl.log";if($LASTEXITCODE){throw 'Ownership directory protection failed'}
Copy-Item "$r\vm\recover-native-desktop-v97.ps1" "$out\recover.ps1"
$ownership=[ordered]@{schema=3;packages=$packages;thumbprint=$thumb;devcon="$r\dependencies\wdk-tools\devcon.exe";devcon_sha256=(Get-FileHash "$r\dependencies\wdk-tools\devcon.exe").Hash;physical_display_before=$before;added_stores=@();owned_processes=@();owned_trace_directories=@();controller_pid=$PID;created=[DateTime]::UtcNow.ToString('o');mode='native-admission'}
function Save-Ownership{$ownership|ConvertTo-Json -Depth 7|Set-Content "$out\ownership.tmp";Move-Item "$out\ownership.tmp" "$out\ownership.json" -Force}
Save-Ownership
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;mode='native-admission';devices_installed=$false;display_topology_requested=$false;physical_output_disabled=$false;boot_settings_changed=$false;memory_integrity_before=$true;staged=@();desktop_offloaded=$false}
function Save-Report{$report|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"}
function Run-Owned([string]$file,[string[]]$arguments,[string]$label,[int]$seconds=90){$quoted=@($arguments|ForEach-Object {'"'+$_.Replace('"','\"')+'"'});$process=Start-Process $file -ArgumentList $quoted -PassThru -NoNewWindow -RedirectStandardOutput "$out\$label.stdout.log" -RedirectStandardError "$out\$label.stderr.log";[void]$process.Handle;if(!$process.WaitForExit($seconds*1000)){$process.Kill();$process.WaitForExit();throw "Owned $label tool timed out"};$process.Refresh();$code=$process.ExitCode;$process.Dispose();return $code}
Save-Report;$armed=$false
try{
 $action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File `"$out\recover.ps1`" -Root `"$out`""
 $triggers=@((New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(2370)),(New-ScheduledTaskTrigger -AtStartup));$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 5) -MultipleInstances IgnoreNew -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries;$identity=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
 Register-ScheduledTask -TaskName $task -Action $action -Trigger $triggers -Settings $settings -Principal $identity|Out-Null;$armed=$true;$report.recovery_task_armed=$true;Save-Report
 $gate=(& $probe --inspect-session)|ConvertFrom-Json;if($LASTEXITCODE -or !$gate.ready_for_interactive_task){throw 'Console no longer ready'}
 $dwmSid="S-1-5-90-0-$($gate.console_session)"
 foreach($name in @('T8-NpuRender-NativeTraceV1','T8-NpuRender-NativeIddV1')){
  $ownership.owned_trace_directories+=$name;Save-Ownership
  $directory="$env:ProgramData\$name";New-Item -ItemType Directory $directory|Out-Null
  $acl=New-Object System.Security.AccessControl.DirectorySecurity
  $extra=if($name -eq 'T8-NpuRender-NativeTraceV1'){"(A;OICI;0x1301bf;;;$dwmSid)"}else{''}
  $acl.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;LS)$extra");Set-Acl $directory -AclObject $acl
 }
 $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new("$r\vm\native-driver-package-v37\test-signing.cer");if($cert.Thumbprint -ne $thumb){throw 'Certificate mismatch'}
 foreach($name in @('Root','TrustedPublisher')){if(!(Test-Path "Cert:\LocalMachine\$name\$thumb")){$ownership.added_stores+=$name;Save-Ownership;$store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine');$store.Open('ReadWrite');try{$store.Add($cert)}finally{$store.Close()};if(!(Test-Path "Cert:\LocalMachine\$name\$thumb")){throw 'Temporary trust unavailable'}}}
 foreach($p in $packages){
  $code=Run-Owned "$env:windir\System32\pnputil.exe" @('/add-driver',"$r\vm\$($p.folder)\$($p.inf)") $p.folder;if($code){throw 'Package staging failed'}
  $staged=@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq $p.inf -and $_.Version -eq $p.version})
  if($staged.Count -ne 1 -or $staged[0].Driver -notmatch '^oem\d+\.inf$' -or (Get-FileHash "$env:windir\INF\$($staged[0].Driver)").Hash -ne $p.inf_sha256){throw 'Staged package identity mismatch'}
  $report.staged+=@{inf=$p.inf;published_inf=$staged[0].Driver;version=$p.version};Save-Report
 }
 $gate=(& $probe --inspect-session)|ConvertFrom-Json;if($LASTEXITCODE -or !$gate.ready_for_interactive_task){throw 'Console no longer ready'}
 $renderer=$packages[0]
 $code=Run-Owned $ownership.devcon @('install',"$r\vm\$($renderer.folder)\$($renderer.inf)",$renderer.hardware_id) 'install-renderer'
 if($code){throw 'Native renderer installation failed or requested restart'}
 $report.devices_installed=$true;Save-Report
 $devices=@(Get-PnpDevice -PresentOnly -Class Display|Where-Object {@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data) -contains $renderer.hardware_id})
 if($devices.Count -ne 1 -or $devices[0].Status -ne 'OK'){throw 'Native renderer is not healthy'}
 $key=($devices[0]|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_Driver).Data
 if($key -notmatch '^\{4d36e968-e325-11ce-bfc1-08002be10318\}\\\d{4}$'){throw 'Unexpected renderer class registration'}
 $registration=Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$key"
 $modules=@($registration.UserModeDriverName|Select-Object -Unique);$dll=$modules[0]
 $renderFiles=Get-Content "$r\vm\native-driver-package-v37\sha256.json" -Raw|ConvertFrom-Json
 $expected=($renderFiles|Where-Object name -eq 'nr_native_probe_umd.dll').sha256
 if($modules.Count -ne 1 -or $registration.DriverVersion -ne $renderer.version -or !$dll.StartsWith("$env:windir\System32\DriverStore\FileRepository\",[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($dll) -ne 'nr_native_probe_umd.dll' -or (Get-FileHash $dll).Hash -ne $expected){throw 'Installed renderer identity mismatch'}
 $report.installed_umd_sha256=$expected
 $report.renderer_instance=$devices[0].InstanceId;Save-Report
 $arguments=@("$r\vm\native_desktop_bootstrap_v71.py",'--interactive-dll',$dll,'--monitor-seconds','7200','--recovery-task',$task,'--ptl-complex-blob',"$r\vm\ptl-six-span-complex-shader-v1\patched.blob",'--ptl-rectangle-blob',"$r\vm\ptl-six-span-point-rectangle-v2\patched.blob",'--ptl-long-blob',"$r\vm\ptl-six-span-long-shader-v2\patched.blob",'--ptl-program-blob',"$r\vm\ptl-six-span-program-compact-v1\patched.blob",'--ptl-masked-blob',"$r\vm\ptl-six-span-masked-v1\patched.blob",'--ptl-geometry-blob',"$r\vm\ptl-six-span-geometry-v2\patched.blob",'--ptl-texture-blob',"$r\vm\ptl-six-span-three-texture-v1\patched.blob",'--ptl-solid-blob',"$r\vm\ptl-six-span-solid-v3\patched.blob",'--ptl-four-tap-blob',"$r\vm\ptl-six-span-general-v1\patched.blob",'--memory-cache','1','--texture-scope','1','--frame-blob-dir',"$r\vm\frame-blobs-v18\deploy",'--worker',$worker,'--runtime',"$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs",'--cache',"$r\vm\model-cache-2026-4-v209",'--session-probe',$probe,'--devcon',$ownership.devcon,'--idd-inf',"$r\vm\native-idd-package-v3\nr_native_indirect_probe.inf",'--transaction',$out,'--trace',"$env:ProgramData\T8-NpuRender-NativeIddV1")
 $code=Run-Owned "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" $arguments 'bootstrap' 7320
 $report.bootstrap_exit=$code;Save-Report;if($code){throw 'Native desktop admission trial failed'}
 $trial=Get-Content "$out\bootstrap\report.json" -Raw|ConvertFrom-Json
 if(!$trial.passed -or !$trial.completed -or $trial.error -or !$trial.owned_process_acls_restored_exactly -or !$trial.native_indirect_frame_produced -or !$trial.actual_dwm_npu_execution -or !$trial.preferred_adapter_selected -or !$trial.shared_application_qualified -or !$trial.shared_application.passed){throw 'Native compositor evidence absent'}
 $report.native_indirect_frame_produced=$true;$report.actual_dwm_npu_execution=$true
 $report.operations_passed=$true
}catch{$report.error=$_.Exception.Message}
finally{
 if($armed){try{$code=Run-Owned "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',"$out\recover.ps1",'-Root',$out) 'recovery' 240;$cleanup=Get-Content "$out\recovery-report.json" -Raw|ConvertFrom-Json;$report.cleanup_passed=($code -eq 0 -and $cleanup.passed)}catch{$report.cleanup_passed=$false;$report.cleanup_error=$_.Exception.Message}}
 try{$after=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard;$report.memory_integrity_after=($after.VirtualizationBasedSecurityStatus -eq 2 -and $after.SecurityServicesRunning -contains 2)}catch{$report.memory_integrity_after=$false}
 $report.passed=([bool]$report.operations_passed -and [bool]$report.cleanup_passed -and $report.memory_integrity_after);$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
