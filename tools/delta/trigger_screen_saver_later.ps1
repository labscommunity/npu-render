# Registers an interactive one-shot task that starts the screen saver after -Seconds the way the
# system does: it sets the user's saver to the OLED Care saver and broadcasts WM_SYSCOMMAND
# SC_SCREENSAVE, so Winlogon performs the desktop switch (notification event 6) that the idle
# timer would perform. Launching the .scr directly only opens a window on the default desktop
# and does not reproduce the transition. The task restores the previous saver setting and
# removes itself.
param([int]$Seconds=300,[string]$Saver='C:\Windows\ASUS\ScreenSaver\OLED Care Screensaver.scr',[string]$Output='')
$task="T8-NpuRender-SaverTrigger-$(Get-Date -Format 'HHmmss')"
$inner=@'
$k='HKCU:\Control Panel\Desktop';$old=(Get-ItemProperty $k -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
Set-ItemProperty $k -Name 'SCRNSAVE.EXE' -Value '__SAVER__';Set-ItemProperty $k -Name ScreenSaveActive -Value '1'
Add-Type -Namespace T8 -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern System.IntPtr SendMessage(System.IntPtr h,uint m,System.IntPtr w,System.IntPtr l);'
[T8.U]::SendMessage([IntPtr]0xffff,0x112,[IntPtr]0xF140,[IntPtr]0) | Out-Null
if('__OUTPUT__'){ Set-Content '__OUTPUT__' ("{""saver_requested"":""" + (Get-Date -Format o) + """,""previous"":""" + $old + """}") }
Start-Sleep -Seconds 30
if($old){ Set-ItemProperty $k -Name 'SCRNSAVE.EXE' -Value $old } else { Remove-ItemProperty $k -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue }
Unregister-ScheduledTask -TaskName '__TASK__' -Confirm:$false
'@
$inner=$inner.Replace('__SAVER__',$Saver).Replace('__OUTPUT__',$Output).Replace('__TASK__',$task)
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))
$action=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -WindowStyle Hidden -EncodedCommand $encoded"
$principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 3) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$trigger=New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds($Seconds)
Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Trigger $trigger | Out-Null
[Console]::Out.WriteLine("{""saver_trigger_registered"":true,""task"":""$task"",""at"":""$((Get-Date).AddSeconds($Seconds).ToUniversalTime().ToString('o'))""}")
