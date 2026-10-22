# Stops a running screen saver before a trial that needs the interactive Default desktop.
$p=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*Screensaver*' -or $_.Name -like '*.scr' })
foreach($x in $p){ [Console]::Out.WriteLine("stopping saver pid=$($x.ProcessId) name=$($x.Name)"); Stop-Process -Id $x.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
[Console]::Out.WriteLine("{""screen_saver_processes"":$(@(Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*Screensaver*' -or $_.Name -like '*.scr' }).Count)}")
