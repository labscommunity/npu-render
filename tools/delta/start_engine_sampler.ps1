# Starts engine_sampler.ps1 detached (so a sequence step can start it beside a trial) and returns.
param([Parameter(Mandatory=$true)][string]$Output,[int]$Seconds=900,[int]$Interval=2)
$sampler="$env:LOCALAPPDATA\npu-render\vm\engine_sampler.ps1"
if(Test-Path $Output){Remove-Item $Output -Force}
$p=Start-Process powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$sampler,'-Output',$Output,'-Seconds',"$Seconds",'-Interval',"$Interval") -WindowStyle Hidden -PassThru
[Console]::Out.WriteLine("{""engine_sampler_started"":true,""pid"":$($p.Id),""output"":""$($Output -replace '\\','/')"",""seconds"":$Seconds}")
