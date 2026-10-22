# Waits a few seconds between sequence steps (for example after the readiness observer starts).
param([int]$Seconds=10)
Start-Sleep -Seconds $Seconds
[Console]::Out.WriteLine("{""paused_s"":$Seconds}")
