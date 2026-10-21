# Registers an interactive one-shot task that starts the host's screen saver after -Seconds, so a
# trial can reproduce the screen-saver transition early instead of waiting for the idle timer.
# The saver path defaults to the ASUS OLED Care saver found on Delta; the task runs in the
# console session as the signed-in user and removes itself when done.
param([int]$Seconds=300,[string]$Saver='C:\Windows\ASUS\ScreenSaver\OLED Care Screensaver.scr',[string]$Output='')
$task="T8-NpuRender-SaverTrigger-$(Get-Date -Format 'HHmmss')"
$action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument ('-NoProfile -WindowStyle Hidden -Command "Start-Process -FilePath ''' + $Saver + ''' -ArgumentList ''/s''; if(''' + $Output + '''){ Set-Content ''' + $Output + ''' (Get-Date -Format o) }; Start-Sleep -Seconds 2; Unregister-ScheduledTask -TaskName ''' + $task + ''' -Confirm:$false"')
$principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$trigger=New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds($Seconds)
Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Trigger $trigger | Out-Null
[Console]::Out.WriteLine("{""saver_trigger_registered"":true,""task"":""$task"",""at"":""$((Get-Date).AddSeconds($Seconds).ToUniversalTime().ToString('o'))"",""saver"":""$($Saver -replace '\\','/')""}")
