# Registers one-shot scheduled tasks that stop a running screen saver after each of -Delays
# seconds, so a long admission exercises the live tools' desktop-switch return path (the ASUS
# OLED Care saver runs until input arrives; stopping its process makes winlogon restore the
# Default desktop). Tasks, not detached processes: a process started from SSH dies with it.
param([int[]]$Delays=@(3000),[string]$Output='')
$stamp=Get-Date -Format 'HHmmss';$n=0
foreach($seconds in $Delays){
  $n++;$task="T8-NpuRender-SaverStop-$stamp-$n";$at=(Get-Date).AddSeconds($seconds)
  $inner="`$p=@(Get-CimInstance Win32_Process | Where-Object { `$_.Name -like '*Screensaver*' -or `$_.Name -like '*.scr' }); foreach(`$x in `$p){ Stop-Process -Id `$x.ProcessId -Force -ErrorAction SilentlyContinue }; if('$Output'){ Add-Content '$Output' ('{""saver_stop"":""' + (Get-Date -Format o) + '"",""stopped"":' + `$p.Count + '}') }; Unregister-ScheduledTask -TaskName '$task' -Confirm:`$false"
  $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))
  $action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -NonInteractive -EncodedCommand $encoded"
  $principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType S4U -RunLevel Highest
  $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
  $trigger=New-ScheduledTaskTrigger -Once -At $at
  Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Trigger $trigger | Out-Null
  [Console]::Out.WriteLine("{""saver_stop_scheduled"":true,""task"":""$task"",""at"":""$($at.ToUniversalTime().ToString('o'))""}")
}
