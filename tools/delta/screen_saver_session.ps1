# Queries or sets the console session's live screen saver state through SystemParametersInfo,
# from an interactive one-shot task (the values live in the session, not only the registry; an
# OLED care component sets them through the same API, so a long trial disables the saver here
# and re-enables it afterwards). Writes one JSON line to -Output and prints it.
param([ValidateSet('query','disable','enable')][string]$Action='query',[string]$Output='')
$task="T8-NpuRender-SaverSession-$(Get-Date -Format 'HHmmssfff')"
if(-not $Output){ $Output="$env:LOCALAPPDATA\npu-render\vm\saver-session-$Action.json" }
$inner=@'
Add-Type -Namespace T8 -Name S -MemberDefinition '[DllImport("user32.dll",SetLastError=true)] public static extern bool SystemParametersInfo(uint a,uint p,ref int v,uint f); [DllImport("user32.dll",SetLastError=true)] public static extern bool SystemParametersInfoW(uint a,uint p,System.IntPtr v,uint f);'
$active=0;$timeout=0;$running=0
[T8.S]::SystemParametersInfo(0x0010,0,[ref]$active,0)|Out-Null;[T8.S]::SystemParametersInfo(0x000E,0,[ref]$timeout,0)|Out-Null;[T8.S]::SystemParametersInfo(0x0072,0,[ref]$running,0)|Out-Null
$before="{""active"":$active,""timeout_s"":$timeout,""running"":$running}"
if('__ACTION__' -eq 'disable'){ [T8.S]::SystemParametersInfoW(0x0011,0,[IntPtr]::Zero,2)|Out-Null }
if('__ACTION__' -eq 'enable'){ [T8.S]::SystemParametersInfoW(0x0011,1,[IntPtr]::Zero,2)|Out-Null }
$active2=0;[T8.S]::SystemParametersInfo(0x0010,0,[ref]$active2,0)|Out-Null
$exe=(Get-ItemProperty 'HKCU:\Control Panel\Desktop' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
Set-Content '__OUTPUT__' ("{""action"":""__ACTION__"",""before"":" + $before + ",""active_after"":" + $active2 + ",""scrnsave_exe"":""" + ($exe -replace '\\','/') + """,""at"":""" + (Get-Date -Format o) + """}")
Unregister-ScheduledTask -TaskName '__TASK__' -Confirm:$false
'@
$inner=$inner.Replace('__ACTION__',$Action).Replace('__OUTPUT__',$Output).Replace('__TASK__',$task)
if(Test-Path $Output){ Remove-Item $Output -Force }
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))
$taskAction=New-ScheduledTaskAction -Execute "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-NoProfile -WindowStyle Hidden -EncodedCommand $encoded"
$principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $task -Action $taskAction -Principal $principal -Settings $settings | Out-Null
Start-ScheduledTask -TaskName $task
$deadline=(Get-Date).AddSeconds(30); while(-not (Test-Path $Output) -and (Get-Date) -lt $deadline){ Start-Sleep -Milliseconds 250 }
if(Test-Path $Output){ [Console]::Out.WriteLine((Get-Content $Output -Raw)) } else { [Console]::Out.WriteLine("{""error"":""session task produced no result""}"); exit 1 }
