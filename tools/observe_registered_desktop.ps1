# Finite observation of the owned disposable VM; never run on a physical host.
param(
 [Parameter(Mandatory=$true)][ValidatePattern('^lab-[a-z0-9-]+$')][string]$RenderName,
 [Parameter(Mandatory=$true)][ValidatePattern('^0\.0\.\d+\.0$')][string]$RenderVersion,
 [Parameter(Mandatory=$true)][string]$RenderPackage,
 [Parameter(Mandatory=$true)][ValidatePattern('^lab-[a-z0-9-]+$')][string]$IndirectName,
 [Parameter(Mandatory=$true)][ValidatePattern('^0\.0\.\d+\.0$')][string]$IndirectVersion,
 [Parameter(Mandatory=$true)][string]$IndirectPackage,
 [Parameter(Mandatory=$true)][ValidatePattern('^idd-topology-[a-z0-9-]+$')][string]$TopologyName,
 [Parameter(Mandatory=$true)][string]$TopologyProbe,
 [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$TopologySha256,
 [ValidateRange(5,120)][int]$ObservationSeconds=90
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
. "$PSScriptRoot\lab_device_identity.ps1"
. "$PSScriptRoot\lab_report.ps1"
Assert-NrLabGuest
$prefix="C:\npu-lab\$RenderName-observation";$trace="NpuRender-$RenderName"
if(Test-Path "$prefix.json"){throw 'Evidence exists'}
foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){if(@(Get-NrLabDevice -HardwareId $id).Count){throw 'A custom guest device is already present'}}
if(Test-Path C:\npu-lab\idd-trace){throw 'Prior IDD trace exists'}
New-Item -ItemType Directory C:\npu-lab\idd-trace|Out-Null
& icacls.exe C:\npu-lab\idd-trace /grant '*S-1-5-11:(OI)(CI)M' '*S-1-5-84-0-0-0-0-0:(OI)(CI)M' *> "$prefix-acl.log"
if($LASTEXITCODE -ne 0){throw 'Trace ACL failed'}
@('Microsoft-Windows-DxgKrnl 0xffffffffffffffff 5','Microsoft-Windows-Dwm-Core 0x400021 5','Microsoft-Windows-DXGI 0xffffffffffffffff 5','{D92BCB52-FA78-406F-A9A5-2037509FADEA} 0xcf4 255')|Set-Content "$prefix-providers.txt"
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;desktop_offload_tested=$false;watchdog_configuration_changed=$false;performance_tested=$false}
Write-NrLabReport "$prefix.json" $report
$old=Get-ExecutionPolicy -Scope Process;$running=$false
$management=[Diagnostics.Process]::GetCurrentProcess();$oldPriority=$management.PriorityClass
$management.PriorityClass=[Diagnostics.ProcessPriorityClass]::AboveNormal
try {
 Set-ExecutionPolicy -Scope Process Bypass -Force
 & logman.exe create trace $trace -o "$prefix.etl" -f bincirc -max 96 -bs 64 -nb 8 64 -pf "$prefix-providers.txt" -ets *> "$prefix-start.log"
 if($LASTEXITCODE -ne 0){throw 'Graphics trace failed to start'};$running=$true
 & C:\npu-lab\manage_lab_driver.ps1 -Action install -HardwareId 'ROOT\NPURENDER_KMD_LAB' -Name $RenderName -Version $RenderVersion -Package $RenderPackage | Out-Null
 $r=Get-Content "C:\npu-lab\$RenderName-install.json" -Raw|ConvertFrom-Json;if(!$r.passed){throw 'Render install failed'}
 & C:\npu-lab\manage_lab_driver.ps1 -Action install -HardwareId 'ROOT\NPURENDER_INDIRECT_LAB' -Name $IndirectName -Version $IndirectVersion -Package $IndirectPackage | Out-Null
 $r=Get-Content "C:\npu-lab\$IndirectName-install.json" -Raw|ConvertFrom-Json;if(!$r.passed){throw 'Indirect install failed'}
 & C:\npu-lab\start_indirect_topology_task.ps1 -Probe $TopologyProbe -Sha256 $TopologySha256 -Name $TopologyName -SoleIndirect -ObservationSeconds $ObservationSeconds | Out-Null
 $watch=[Diagnostics.Stopwatch]::StartNew()
 $knownDwm=New-Object 'System.Collections.Generic.HashSet[int]'
 # Native process enumeration and footer reads avoid blocking CIM task polling.
 # Stop promptly after a complete failed DWM draw; retain its input evidence.
 while($watch.ElapsedMilliseconds -lt (($ObservationSeconds+30)*1000)){
  foreach($process in [Diagnostics.Process]::GetProcessesByName('dwm')){[void]$knownDwm.Add($process.Id);$process.Dispose()}
  $failed=$false
  foreach($processId in $knownDwm){
   $file="C:\npu-lab\umd-trace\process-$processId.jsonl"
   if([IO.File]::Exists($file) -and (Select-String -LiteralPath $file -Pattern '"event":"reject"|"event":"present_rejected"|"event":"rotate_rejected"|"event":"present_callback_result","status":"8' -Quiet)){
    $report.stop_reason='dwm-ddi-rejection-recorded';$failed=$true;break
   }
  }
  if($failed){break}
  foreach($file in [IO.Directory]::GetFiles('C:\npu-lab\umd-trace','draw-*.bin')){
   $processId=0;if(![int]::TryParse([IO.Path]::GetFileNameWithoutExtension($file).Substring(5),[ref]$processId) -or !$knownDwm.Contains($processId)){continue}
   $captureStream=$null
   try {
    $captureStream=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    if($captureStream.Length -lt 48){continue}
    [void]$captureStream.Seek(-40,[IO.SeekOrigin]::End);$footer=New-Object byte[] 40
    if($captureStream.Read($footer,0,40) -eq 40 -and [Text.Encoding]::ASCII.GetString($footer,0,8) -eq 'complete' -and [BitConverter]::ToUInt64($footer,32) -eq 0){
     $report.stop_reason='complete-dwm-failure-capture';$report.failed_draw_file=[IO.Path]::GetFileName($file);$failed=$true;break
    }
   } finally {if($captureStream){$captureStream.Dispose()}}
  }
  if($failed){break}
  foreach($iddLog in [IO.Directory]::GetFiles('C:\npu-lab\idd-trace','process-*.jsonl')){
   $lines=(Get-Content -LiteralPath $iddLog -Raw) -split "`n"
   if(@($lines|Where-Object {$_ -match '"event":"frame_readback_failed"|"event":"frame_readback".*"status":"8'}).Count){$report.stop_reason='frame-readback-failure-recorded';$failed=$true;break}
  }
  if($failed){break}
  Start-Sleep -Milliseconds 500
 }
 $report.observation_ms=$watch.ElapsedMilliseconds
 if(!$report.stop_reason){$report.stop_reason='bounded-observation-complete'}
 $report.observation_complete=$true
} catch {$report.error=$_.Exception.Message}
finally {
 # Let the finite topology helper restore its original paths before removing
 # the monitor. It has its own bounded observation and five-minute task cap.
 $restoring=[Diagnostics.Stopwatch]::StartNew()
 while($restoring.ElapsedMilliseconds -lt (($ObservationSeconds+30)*1000)){
  $topology="C:\npu-lab\$TopologyName.jsonl"
  if(!(Test-Path $topology)){break}
  try {if((Get-Content $topology -Raw) -match '"passed":(true|false)'){break}}catch {$report.topology_read_error=$_.Exception.Message;break}
  Start-Sleep -Milliseconds 500
 }
 $report.topology_wait_ms=$restoring.ElapsedMilliseconds
 $report.phase='cleanup';Write-NrLabReport "$prefix.json" $report -BestEffort
 if($running){try {& logman.exe stop $trace -ets *> "$prefix-stop.log";$report.trace_stop_exit=$LASTEXITCODE}catch {$report.trace_stop_error=$_.Exception.Message}}
 foreach($item in @(@($IndirectName,'ROOT\NPURENDER_INDIRECT_LAB',$IndirectVersion,$IndirectPackage),@($RenderName,'ROOT\NPURENDER_KMD_LAB',$RenderVersion,$RenderPackage))){
  if(Test-Path "C:\npu-lab\$($item[0])-owned.json"){
   try {
    & C:\npu-lab\manage_lab_driver.ps1 -Action remove -Name $item[0] -HardwareId $item[1] -Version $item[2] -Package $item[3] | Out-Null
    $removal=Get-Content "C:\npu-lab\$($item[0])-remove.json" -Raw|ConvertFrom-Json
    if(!$removal.completed -or !$removal.passed){throw 'Owned device removal did not pass'}
    $report[($item[0]+'-removed')]=$true
   }
   catch {$report[($item[0]+'-remove-error')]=$_.Exception.Message}
  }
 }
 $report.cleanup_passed=[bool]($report[($RenderName+'-removed')] -and $report[($IndirectName+'-removed')])
 try {$management.PriorityClass=$oldPriority;$management.Dispose()}catch {$report.priority_restore_error=$_.Exception.Message}
 try {Set-ExecutionPolicy -Scope Process $old -Force}catch {$report.policy_restore_error=$_.Exception.Message}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Write-NrLabReport "$prefix.json" $report -BestEffort
}
$report|ConvertTo-Json -Depth 12
if($report.error -or !$report.cleanup_passed){throw 'Desktop observation or cleanup failed; inspect preserved evidence'}
