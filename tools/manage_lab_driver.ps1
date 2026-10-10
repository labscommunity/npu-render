param(
 [Parameter(Mandatory=$true)][ValidateSet('install','remove')][string]$Action,
 [Parameter(Mandatory=$true)][ValidateSet('ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB')][string]$HardwareId,
 [Parameter(Mandatory=$true)][ValidatePattern('^lab-[a-z0-9-]+$')][string]$Name,
 [Parameter(Mandatory=$true)][ValidatePattern('^0\.0\.\d+\.0$')][string]$Version,
 [Parameter(Mandatory=$true)][string]$Package
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
. "$PSScriptRoot\lab_device_identity.ps1"
. "$PSScriptRoot\lab_report.ps1"
Assert-NrLabGuest
$Package=[IO.Path]::GetFullPath($Package)
if(!$Package.StartsWith('C:\npu-lab\driver-package-',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path $Package -PathType Container) -or ((Get-Item $Package).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Owned package directory required'}
$root='C:\npu-lab';$prefix="$root\$Name-$Action";$savedPath="$root\$Name-owned.json"
if(Test-Path "$prefix.json"){throw 'Action evidence already exists'}
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');action=$Action;hardware_id=$HardwareId;expected_version=$Version;completed=$false;passed=$false}
Write-NrLabReport "$prefix.json" $report
try {
 $current=@(Get-NrLabDevice -HardwareId $HardwareId)
 $current | ConvertTo-Json -Depth 5 | Set-Content "$prefix-before.json"
 if($Action -eq 'install'){
  if($current.Count -or (Test-Path $savedPath)){throw 'Device or saved ownership already exists'}
  foreach($file in (Get-Content "$Package\sha256.json" -Raw | ConvertFrom-Json)){
   if($file.name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash (Join-Path $Package $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Package hash mismatch'}
  }
  $inf=if($HardwareId -eq 'ROOT\NPURENDER_KMD_LAB'){'nr_render_lab.inf'}else{'nr_indirect_lab.inf'}
  $trace=if($HardwareId -eq 'ROOT\NPURENDER_KMD_LAB'){'umd-trace'}else{'idd-trace'}
  if(!(Test-Path "$root\$trace" -PathType Container)){throw 'Prepared trace directory required'}
  & "$root\devcon.exe" install (Join-Path $Package $inf) $HardwareId *> "$prefix-devcon.log"
  $report.devcon_exit=$LASTEXITCODE
  $current=@(Get-NrLabDevice -HardwareId $HardwareId)
  $current | ConvertTo-Json -Depth 5 | Set-Content "$prefix-after.json"
  if($current.Count -ne 1){throw 'Expected exactly one owned device after installation'}
  $current[0] | ConvertTo-Json -Depth 5 | Set-Content $savedPath
  if($report.devcon_exit -ne 0 -or !$current[0].started -or $current[0].version -ne $Version){throw 'Installed device did not reach expected healthy version'}
  if($HardwareId -eq 'ROOT\NPURENDER_KMD_LAB'){
   $expected=(Get-FileHash "$Package\nr_fill_umd.dll" -Algorithm SHA256).Hash
   foreach($module in $current[0].user_mode_driver){
    if(!$module.StartsWith('C:\Windows\System32\DriverStore\FileRepository\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($module) -ne 'nr_fill_umd.dll' -or (Get-FileHash $module -Algorithm SHA256).Hash -ne $expected){throw 'Installed UMD mismatch'}
   }
   if(!$current[0].user_mode_driver){throw 'Registered UMD missing'}
   $report.umd_sha256=$expected
  }
 } else {
  $saved=Get-Content $savedPath -Raw | ConvertFrom-Json
  if($current.Count -ne 1 -or $current[0].instance -ne $saved.instance -or $current[0].inf -ne $saved.inf -or $current[0].version -ne $Version -or $saved.hardware_id -ne $HardwareId){throw 'Saved removal identity mismatch'}
  & "$root\devcon.exe" remove "@$($saved.instance)" *> "$prefix-devcon.log"
  $report.devcon_exit=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'Device removal failed'}
  if(@(Get-NrLabDevice -HardwareId $HardwareId).Count){throw 'Owned device still present'}
  & pnputil.exe /delete-driver $saved.inf *> "$prefix-pnputil.log"
  $report.pnputil_exit=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'Owned INF deletion failed'}
 }
 $report.passed=$true
} catch {$report.error=$_.Exception.Message}
finally {
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o')
 Write-NrLabReport "$prefix.json" $report
}
Get-Content "$prefix.json"
if(!$report.passed){exit 1}
