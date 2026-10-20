param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$statePath=Join-Path $Root 'ownership.json';$state=Get-Content $statePath -Raw|ConvertFrom-Json
if($state.schema -ne 1 -or $state.hardware_id -ne 'ROOT\NPURENDER_NATIVE_PROBE' -or $state.version -ne '0.0.1319.0' -or $state.thumbprint -ne '3112355AF6B6C6265E9B849662D3E02F6797343A'){throw 'Recovery ownership contract mismatch'}
$mutex=[Threading.Mutex]::new($false,'Global\T8.NpuRender.NativeRegistrationRecovery.v3');$held=$false
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;devices_removed=@();packages_removed=@();certificate_stores_restored=@();errors=@()}
try {
 try{$held=$mutex.WaitOne(5000)}catch [Threading.AbandonedMutexException]{$held=$true};if(!$held){throw 'Another recovery owns cleanup'}
 if((Get-FileHash $state.devcon).Hash -ne $state.devcon_sha256){throw 'Recovery utility changed'}
 # An exact diagnostic hardware ID was absent before this transaction.
 foreach($device in @(Get-PnpDevice -PresentOnly -Class Display)){
  $ids=@(($device|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue).Data)
  if($ids -notcontains $state.hardware_id){continue}
  if($device.InstanceId -notmatch '^ROOT\\DISPLAY\\[0-9A-F]+$'){throw 'Unexpected diagnostic instance path'}
  & $state.devcon remove "@$($device.InstanceId)" *>> "$Root\recovery-devcon.log";if($LASTEXITCODE){throw 'Diagnostic device removal failed or requires restart'}
  $report.devices_removed+= $device.InstanceId
 }
 foreach($driver in @(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq 'nr_native_probe.inf' -and $_.Version -eq $state.version})){
  if($driver.Driver -notmatch '^oem\d+\.inf$' -or (Get-FileHash "$env:windir\INF\$($driver.Driver)").Hash -ne $state.inf_sha256){throw 'Diagnostic INF removal identity mismatch'}
  & pnputil.exe /delete-driver $driver.Driver *>> "$Root\recovery-pnputil.log";if($LASTEXITCODE){throw 'Diagnostic package deletion failed'}
  $report.packages_removed+=$driver.Driver
 }
 # Restore only memberships absent before this transaction.
 foreach($name in @($state.added_stores)){
  if($name -notin @('Root','TrustedPublisher')){throw 'Unexpected owned certificate store'}
  $store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine');$store.Open('ReadWrite')
  try {foreach($cert in @($store.Certificates|Where-Object Thumbprint -eq $state.thumbprint)){$store.Remove($cert)}}finally{$store.Close()}
  if(Test-Path "Cert:\LocalMachine\$name\$($state.thumbprint)"){throw 'Certificate membership restoration failed'}
  $report.certificate_stores_restored+=$name
 }
 $remaining=@(Get-PnpDevice -PresentOnly -Class Display|Where-Object {@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue).Data) -contains $state.hardware_id})
 if($remaining.Count){throw 'Diagnostic adapter remains present'}
 foreach($prior in $state.physical_display_before){$now=Get-PnpDevice -InstanceId $prior.instance -PresentOnly;if($now.Status -ne $prior.status){throw 'Original display device health changed'}}
 $report.passed=$true
 Unregister-ScheduledTask -TaskName 'T8-NpuRender-NativeRecovery-v48' -Confirm:$false -ErrorAction SilentlyContinue
}catch{$report.errors+= $_.Exception.Message}
finally {
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 6|Set-Content "$Root\recovery-report.json"
 if($held){$mutex.ReleaseMutex()};$mutex.Dispose()
}
$report|ConvertTo-Json -Depth 6
if(!$report.passed){exit 1}
