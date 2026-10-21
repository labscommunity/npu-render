# Registers a one-shot interactive task (the console user's desktop, limited run level) that
# starts -Command after -Seconds through the probe runner, appending the child's stdout rows
# and exit code to -Log. Mid-admission client probes (an ordinary application opening the NPU
# adapter, a lease-holding consumer) are started this way so they are ordinary interactive
# processes, not children of the controller or of an SSH session.
param([Parameter(Mandatory=$true)][string[]]$Command,[Parameter(Mandatory=$true)][string]$Log,[int]$Seconds=60,[int]$LimitMinutes=20,[string]$Name='')
$source="$env:LOCALAPPDATA\Temp\npu-graphics-probe-0146a2388f25"
if(!(Test-Path "$source\native_runner.py")){ throw 'probe runner missing' }
if(!$Name){ $Name="T8-NpuRender-Interactive-$(Get-Date -Format 'HHmmssfff')" }
$dir=Split-Path $Log -Parent; if(!(Test-Path $dir)){ New-Item -ItemType Directory $dir | Out-Null }
$job=Join-Path $dir ("job-" + [IO.Path]::GetFileNameWithoutExtension($Log) + ".json")
@{command=$Command;log=$Log}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 $job
$action=New-ScheduledTaskAction -Execute "$source\venv\Scripts\pythonw.exe" -Argument ('"'+$source+'\native_runner.py" "'+$job+'"')
$principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes $LimitMinutes) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -DeleteExpiredTaskAfter (New-TimeSpan -Minutes 1)
$at=(Get-Date).AddSeconds($Seconds);$trigger=New-ScheduledTaskTrigger -Once -At $at;$trigger.EndBoundary=$at.AddMinutes($LimitMinutes+5).ToString('s')
Register-ScheduledTask -TaskName $Name -Action $action -Principal $principal -Settings $settings -Trigger $trigger | Out-Null
[Console]::Out.WriteLine("{""interactive_scheduled"":true,""task"":""$Name"",""at"":""$($at.ToUniversalTime().ToString('o'))"",""log"":""$($Log -replace '\\','/')""}")
