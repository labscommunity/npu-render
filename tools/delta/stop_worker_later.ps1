# Stops the NPU worker processes after -Seconds, detached, so a probe can show what the desktop
# does when the worker disappears mid-admission (the driver must keep DWM alive on its CPU
# fallback; the controller restores the display).
param([int]$Seconds=240,[string]$Output='')
$inner="Start-Sleep -Seconds $Seconds; `$p=@(Get-Process nr_registered_local_indexed_worker -ErrorAction SilentlyContinue); `$p | Stop-Process -Force -ErrorAction SilentlyContinue; if('$Output'){ Set-Content '$Output' ('{""worker_stopped"":""' + (Get-Date -Format o) + '"",""count"":' + `$p.Count + '}') }"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))
$p=Start-Process powershell -ArgumentList @('-NoProfile','-WindowStyle','Hidden','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru
[Console]::Out.WriteLine("{""worker_stop_scheduled"":true,""pid"":$($p.Id),""in_s"":$Seconds}")
