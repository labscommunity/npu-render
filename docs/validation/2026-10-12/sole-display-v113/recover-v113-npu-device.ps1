$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\v113-npu-device-recovery"
if(Test-Path $out){throw 'Recovery evidence exists'}
$devices=@(Get-PnpDevice -PresentOnly -Class ComputeAccelerator|Where-Object{$_.FriendlyName -eq 'Intel(R) NPU' -and $_.InstanceId -like 'PCI\VEN_8086&DEV_B03E\*'})
# The full hardware ID includes subsystem/revision after the device ID.
if(!$devices.Count){$devices=@(Get-PnpDevice -PresentOnly -Class ComputeAccelerator|Where-Object{$_.FriendlyName -eq 'Intel(R) NPU' -and $_.InstanceId -like 'PCI\VEN_8086&DEV_B03E&*'})}
if($devices.Count -ne 1){throw 'Unique Panther Lake NPU required'}
foreach($name in 'nr_registered_draw_replay_probe','nr_registered_indexed_stream_worker'){
 foreach($p in Get-Process -Name $name -ErrorAction SilentlyContinue){if(!$p.HasExited){throw 'Owned workload still running'}}
}
New-Item -ItemType Directory $out|Out-Null
$device=$devices[0];$properties=Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_DriverVersion,DEVPKEY_Device_DriverInfPath
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');instance_id=$device.InstanceId;name=$device.FriendlyName;class=$device.Class;before_status=$device.Status;driver_properties=@($properties|Select-Object KeyName,Data);action='pnputil /restart-device exact NPU instance';reboot_requested=$false;driver_package_changed=$false;gpu_device_touched=$false;reason='NPU replay timed out; terminated test processes remain present and controller output does not close.';documentation='https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax#restart-device'}
$report|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"
$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$env:WINDIR\System32\pnputil.exe";$info.Arguments='/restart-device "'+$device.InstanceId+'"';$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
$p=[Diagnostics.Process]::new();$p.StartInfo=$info;[void]$p.Start();$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$report.command_pid=$p.Id
$finished=$p.WaitForExit(120000);$report.command_finished=$finished
if($finished){$report.exit_code=$p.ExitCode;[IO.File]::WriteAllText("$out\stdout.log",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\stderr.log",$stderr.GetAwaiter().GetResult());Get-Content "$out\stdout.log";Get-Content "$out\stderr.log"}
else{$report.command_timed_out=$true;$report.note='Device action left pending for diagnosis; no reboot requested.'}
$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 6|Set-Content "$out\report.json";$report|ConvertTo-Json -Depth 6
