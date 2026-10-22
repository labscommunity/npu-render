# Stops (or restarts) the ASUS OLED Care components that start a screen saver on an idle OLED
# panel. The registry timer (OLEDProtectScreensaver) is not re-read while the service runs,
# so a long trial stops the processes for its duration and restarts the ASUS Optimization task
# afterwards. Records what it did as one JSON line.
param([ValidateSet('stop','start')][string]$Action='stop',[string]$Record='')
$names=@('AsusOptimization','AsusOLEDShifter')
$result=[ordered]@{action=$Action;stopped=@();started=@()}
if($Action -eq 'stop'){
  foreach($n in $names){ Get-Process $n -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue; $result.stopped+="$n($($_.Id))" } }
  Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*Screensaver*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue; $result.stopped+="saver($($_.ProcessId))" }
} else {
  $task=Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object { $_.TaskName -like 'ASUS Optimization*' } | Select-Object -First 1
  if($task){ Start-ScheduledTask -TaskName $task.TaskName -TaskPath $task.TaskPath; $result.started+=$task.TaskName }
}
$line=($result|ConvertTo-Json -Compress); if($Record){ Set-Content $Record $line }; [Console]::Out.WriteLine($line)
