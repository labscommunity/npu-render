param(
 [Parameter(Mandatory=$true)][string]$Probe,
 [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$Sha256,
 [Parameter(Mandatory=$true)][ValidatePattern('^idd-topology-[a-z0-9-]+$')][string]$Name,
 [switch]$SystemRendererControl,
 [switch]$SoleIndirect,
 [ValidateRange(5,1500)][int]$ObservationSeconds=45
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
. "$PSScriptRoot\lab_device_identity.ps1"
Assert-NrLabGuest
$Probe=[IO.Path]::GetFullPath($Probe)
if(!$Probe.StartsWith('C:\npu-lab\',[StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash $Probe -Algorithm SHA256).Hash -ne $Sha256){throw 'Unexpected topology executable'}
if(@(Get-ChildItem "C:\npu-lab\$Name*").Count){throw 'Evidence name already used'}
$devices=@()
foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){
 $found=@(Get-NrLabDevice -HardwareId $id)
 if($SystemRendererControl -and $id -eq 'ROOT\NPURENDER_KMD_LAB'){
  if($found.Count){throw 'System-renderer control requires the experimental render device removed'}
  continue
 }
 if($found.Count -ne 1 -or !$found[0].started){throw 'Both owned adapters must be healthy'}
 $devices += $found[0]
}
$service=New-Object -ComObject Schedule.Service
$service.Connect()
$definition=$service.NewTask(0)
$definition.RegistrationInfo.Description='Finite owned-guest display activation and restoration diagnostic'
$definition.Principal.UserId='NR-GFX-LAB\npu-lab'
$definition.Principal.LogonType=3 # TASK_LOGON_INTERACTIVE_TOKEN
$definition.Principal.RunLevel=1
$definition.Settings.ExecutionTimeLimit='PT26M'
$definition.Settings.DisallowStartIfOnBatteries=$false
$definition.Settings.StopIfGoingOnBatteries=$false
$action=$definition.Actions.Create(0)
$action.Path=$Probe;$action.Arguments="--report $Name"
if($SoleIndirect){$action.Arguments+=' --sole-indirect'}
if($ObservationSeconds -ne 45){$action.Arguments+=" --seconds $ObservationSeconds"}
$taskName="NpuRender-$Name"
# TASK_CREATE fails on an existing task; never replace another run's evidence.
$task=$service.GetFolder('\').RegisterTaskDefinition($taskName,$definition,2,'NR-GFX-LAB\npu-lab',$null,3)
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');task=$taskName;probe_sha256=$Sha256;devices=$devices;system_renderer_control=[bool]$SystemRendererControl;sole_indirect_requested=[bool]$SoleIndirect;observation_seconds=$ObservationSeconds;maximum_seconds=1560;desktop_offload_tested=$false}
$report | ConvertTo-Json -Depth 6 | Set-Content "C:\npu-lab\$Name-launch.json"
$task.Run($null) | Out-Null
Get-Content "C:\npu-lab\$Name-launch.json"
