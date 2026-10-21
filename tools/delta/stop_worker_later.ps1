# Registers a one-shot scheduled task that stops the NPU worker processes after -Seconds, so a
# probe can show what the desktop does when the worker disappears mid-admission (the driver
# must keep DWM alive on its CPU fallback; the controller restores the display). A task, not a
# detached process: a process started from an SSH session dies with the session.
param([int]$Seconds=240,[string]$Output='')
$task="T8-NpuRender-WorkerStop-$(Get-Date -Format 'HHmmss')"
$inner="`$p=@(Get-Process nr_registered_local_indexed_worker -ErrorAction SilentlyContinue); `$p | Stop-Process -Force -ErrorAction SilentlyContinue; if('$Output'){ Set-Content '$Output' ('{""worker_stopped"":""' + (Get-Date -Format o) + '"",""count"":' + `$p.Count + '}') }; Unregister-ScheduledTask -TaskName '$task' -Confirm:`$false"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))
$action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -NonInteractive -EncodedCommand $encoded"
$principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType S4U -RunLevel Highest
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$trigger=New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds($Seconds)
Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Trigger $trigger | Out-Null
[Console]::Out.WriteLine("{""worker_stop_scheduled"":true,""task"":""$task"",""at"":""$((Get-Date).AddSeconds($Seconds).ToUniversalTime().ToString('o'))""}")
