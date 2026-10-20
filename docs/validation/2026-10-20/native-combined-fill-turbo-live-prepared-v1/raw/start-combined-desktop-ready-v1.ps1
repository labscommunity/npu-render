$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$name='NpuRender-CombinedDesktopReady-v1'
if(Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue){throw 'Task already exists'}
if((Get-FileHash "$vm/run-combined-desktop-ready-v1.ps1").Hash.ToLower() -ne '59cbee252b54875c3f6cf7c9c4d9874a97e76a99729cff1d12f66f3a0cb1b549'){throw 'Readiness source mismatch'}
$tokens=$null;$errors=$null
[Management.Automation.Language.Parser]::ParseFile("$vm/run-combined-desktop-ready-v1.ps1",[ref]$tokens,[ref]$errors)|Out-Null
if($errors.Count){throw 'Coordinator syntax invalid'}
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$vm+'\run-combined-desktop-ready-v1.ps1"') -WorkingDirectory $vm
$principal=New-ScheduledTaskPrincipal -UserId $identity.Name -LogonType S4U -RunLevel Highest
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Hours 3) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings|Out-Null
Export-ScheduledTask -TaskName $name|Set-Content "$vm/combined-desktop-ready-v1-task.xml"
Start-ScheduledTask -TaskName $name
[ordered]@{task=$name;started_utc=[DateTime]::UtcNow.ToString('o');logon_type='S4U';run_level='Highest';recurrent_trigger=$false;execution_limit_hours=3;ssh_lifetime_independent=$true;state=[string](Get-ScheduledTask -TaskName $name).State}|ConvertTo-Json|Set-Content "$vm/combined-desktop-ready-v1-task-start.json"
Get-Content "$vm/combined-desktop-ready-v1-task-start.json" -Raw
